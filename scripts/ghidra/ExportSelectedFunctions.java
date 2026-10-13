// Export only explicitly requested function entries into a new local directory.
// Run headlessly with -noanalysis; this script never starts broad auto-analysis.
// Arguments: NEW_OUTPUT_DIRECTORY HEX_ENTRY_ADDRESS [HEX_ENTRY_ADDRESS ...]
// @category Halftrack.Analysis

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.util.headless.HeadlessScript;
import ghidra.framework.Application;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.util.task.TimeoutTaskMonitor;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.nio.file.StandardOpenOption;
import java.security.MessageDigest;
import java.time.Instant;
import java.util.ArrayList;
import java.util.HexFormat;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.TimeUnit;

public class ExportSelectedFunctions extends HeadlessScript {
    private static final int TIMEOUT_SECONDS = 30;
    private final Gson gson = new GsonBuilder().setPrettyPrinting().serializeNulls().create();

    @Override
    protected void run() throws Exception {
        if (currentProgram == null || !isRunningHeadless()) {
            throw new IllegalArgumentException("A current program in a headless project is required");
        }
        enableHeadlessAnalysis(false);
        String[] args = getScriptArgs();
        if (args.length < 2) {
            throw new IllegalArgumentException("Expected NEW_OUTPUT_DIRECTORY and explicit hex entry addresses");
        }
        Map<String, Address> requested = new LinkedHashMap<>();
        for (int index = 1; index < args.length; index++) {
            String raw = args[index].replaceFirst("(?i)^0x", "");
            if (!raw.matches("[0-9a-fA-F]{1,16}")) {
                throw new IllegalArgumentException("Not a 64-bit hex entry address: " + args[index]);
            }
            Address address = currentProgram.getAddressFactory().getDefaultAddressSpace()
                .getAddress(Long.parseUnsignedLong(raw, 16));
            String key = addressKey(address);
            if (requested.putIfAbsent(key, address) != null) {
                throw new IllegalArgumentException("Duplicate entry address: " + key);
            }
        }
        Path output = Path.of(args[0]).toAbsolutePath().normalize();
        Files.createDirectory(output); // Deliberately fails if anything already exists here.
        Map<String, Object> manifest = new LinkedHashMap<>();
        manifest.put("schema_version", 1);
        manifest.put("started_utc", Instant.now().toString());
        manifest.put("program_sha256", currentProgram.getExecutableSHA256());
        manifest.put("program_format", currentProgram.getExecutableFormat());
        manifest.put("language_id", currentProgram.getLanguageID().toString());
        manifest.put("compiler_spec_id", currentProgram.getCompilerSpec().getCompilerSpecID().toString());
        manifest.put("compiler_identification", currentProgram.getCompiler());
        manifest.put("ghidra_version", Application.getApplicationVersion());
        manifest.put("image_base", addressKey(currentProgram.getImageBase()));
        manifest.put("decompile_timeout_seconds", TIMEOUT_SECONDS);
        manifest.put("preparation_timeout_seconds", TIMEOUT_SECONDS);
        manifest.put("broad_auto_analysis", false);
        manifest.put("requested_count", requested.size());
        manifest.put("completed_count", 0);
        manifest.put("failed_count", 0);
        Map<String, Object> results = new LinkedHashMap<>();
        for (String key : requested.keySet()) {
            Map<String, Object> pending = new LinkedHashMap<>();
            pending.put("status", "pending");
            results.put(key, pending);
        }
        manifest.put("functions", results);
        saveManifest(output, manifest);

        DecompInterface decompiler = new DecompInterface();
        int completed = 0;
        int failed = 0;
        try {
            boolean opened = decompiler.openProgram(currentProgram);
            for (Map.Entry<String, Address> request : requested.entrySet()) {
                Map<String, Object> result = new LinkedHashMap<>();
                long started = System.nanoTime();
                result.put("status", "failed");
                result.put("stage", "prepare");
                result.put("timed_out", false);
                result.put("cancelled", false);
                try {
                    monitor.checkCancelled();
                    if (!opened) {
                        result.put("stage", "open_decompiler");
                        throw new IllegalStateException(decompiler.getLastMessage());
                    }
                    Function function = prepareFunction(request.getValue(), result);
                    result.put("function_name", function.getName());
                    result.put("entry_address", addressKey(function.getEntryPoint()));
                    recordBody(function, result);
                    result.put("stage", "decompile");
                    decompiler.flushCache();
                    DecompileResults decompiled = decompiler.decompileFunction(function, TIMEOUT_SECONDS, monitor);
                    if (decompiled == null) {
                        throw new IllegalStateException("Decompiler returned no result: " + decompiler.getLastMessage());
                    }
                    result.put("timed_out", decompiled.isTimedOut());
                    result.put("cancelled", decompiled.isCancelled());
                    result.put("failed_to_start", decompiled.failedToStart());
                    result.put("decompiler_message", decompiled.getErrorMessage());
                    if (decompiled.decompileCompleted() && decompiled.getDecompiledFunction() != null) {
                        result.put("stage", "write_output");
                        String filename = request.getKey() + ".c";
                        String text = decompiled.getDecompiledFunction().getC();
                        Files.writeString(output.resolve(filename), text, StandardCharsets.UTF_8,
                                          StandardOpenOption.CREATE_NEW);
                        result.put("output_file", filename);
                        result.put("output_sha256", sha256(text.getBytes(StandardCharsets.UTF_8)));
                        result.put("status", "completed");
                        result.put("stage", "complete");
                    }
                    else {
                        result.put("error", decompiled.getErrorMessage());
                    }
                }
                catch (Exception error) {
                    result.put("error_type", error.getClass().getName());
                    result.put("error", error.getMessage());
                    if (monitor.isCancelled()) {
                        result.put("cancelled", true);
                    }
                }
                result.put("elapsed_ms", TimeUnit.NANOSECONDS.toMillis(System.nanoTime() - started));
                results.put(request.getKey(), result);
                if ("completed".equals(result.get("status"))) {
                    completed++;
                }
                else {
                    failed++;
                }
                manifest.put("completed_count", completed);
                manifest.put("failed_count", failed);
                saveManifest(output, manifest);
                println(request.getKey() + ": " + result.get("status") + " (" + result.get("stage") + ")");
            }
        }
        finally {
            decompiler.dispose();
            manifest.put("finished_utc", Instant.now().toString());
            saveManifest(output, manifest);
        }
        println("Requested " + requested.size() + "; completed " + completed + "; failed " + failed);
    }

    private Function prepareFunction(Address entry, Map<String, Object> result) throws Exception {
        MemoryBlock block = currentProgram.getMemory().getBlock(entry);
        if (block == null || !block.isInitialized() || !block.isExecute()) {
            throw new IllegalArgumentException("Entry is not in initialized executable memory");
        }
        Function function = currentProgram.getFunctionManager().getFunctionAt(entry);
        if (function != null) {
            result.put("created_function", false);
            return function;
        }
        if (currentProgram.getFunctionManager().getFunctionContaining(entry) != null) {
            throw new IllegalArgumentException("Entry lies inside a different existing function");
        }
        TimeoutTaskMonitor preparation = TimeoutTaskMonitor.timeoutIn(TIMEOUT_SECONDS, TimeUnit.SECONDS);
        try {
            // Restrict flow decoding to this executable block and disable analysis scheduling.
            DisassembleCommand disassemble = new DisassembleCommand(entry,
                new AddressSet(block.getStart(), block.getEnd()), true);
            disassemble.enableCodeAnalysis(false);
            boolean decoded = disassemble.applyTo(currentProgram, preparation);
            result.put("disassembly_message", disassemble.getStatusMsg());
            result.put("disassembled_bytes", disassemble.getDisassembledAddressSet().getNumAddresses());
            preparation.checkCancelled();
            if (!decoded || currentProgram.getListing().getInstructionAt(entry) == null) {
                throw new IllegalStateException("Could not disassemble entry: " + disassemble.getStatusMsg());
            }
            CreateFunctionCmd create = new CreateFunctionCmd(entry, false);
            boolean created = create.applyTo(currentProgram, preparation);
            preparation.checkCancelled();
            function = currentProgram.getFunctionManager().getFunctionAt(entry);
            if (!created || function == null) {
                throw new IllegalStateException("Could not create function: " + create.getStatusMsg());
            }
            result.put("created_function", true);
            return function;
        }
        finally {
            result.put("preparation_timed_out", preparation.didTimeout());
            if (preparation.didTimeout()) {
                result.put("timed_out", true);
            }
            preparation.finished();
        }
    }

    private void recordBody(Function function, Map<String, Object> result) throws Exception {
        result.put("body_bytes", function.getBody().getNumAddresses());
        List<Map<String, Object>> ranges = new ArrayList<>();
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        byte[] buffer = new byte[4096];
        for (AddressRange range : function.getBody().getAddressRanges()) {
            Map<String, Object> item = new LinkedHashMap<>();
            item.put("start", addressKey(range.getMinAddress()));
            item.put("end_inclusive", addressKey(range.getMaxAddress()));
            item.put("bytes", range.getLength());
            ranges.add(item);
            long remaining = range.getLength();
            Address cursor = range.getMinAddress();
            while (remaining > 0) {
                monitor.checkCancelled();
                int length = (int) Math.min(buffer.length, remaining);
                int read = currentProgram.getMemory().getBytes(cursor, buffer, 0, length);
                if (read != length) {
                    throw new IllegalStateException("Incomplete function-body memory read");
                }
                digest.update(buffer, 0, length);
                remaining -= length;
                if (remaining > 0) {
                    cursor = cursor.addNoWrap(length);
                }
            }
        }
        result.put("body_ranges", ranges);
        result.put("body_sha256", HexFormat.of().formatHex(digest.digest()));
        result.put("body_hash_order", "concatenated bytes of address ranges in ascending order");
    }

    private static String addressKey(Address address) {
        return "0x" + Long.toUnsignedString(address.getOffset(), 16);
    }

    private static String sha256(byte[] bytes) throws Exception {
        return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));
    }

    private void saveManifest(Path output, Map<String, Object> manifest) throws Exception {
        Path pending = output.resolve("manifest.json.pending");
        Files.writeString(pending, gson.toJson(manifest) + "\n", StandardCharsets.UTF_8);
        Files.move(pending, output.resolve("manifest.json"),
                   StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
    }
}
