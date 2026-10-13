// Original address-based batch exporter. Outputs are local, immutable evidence.
// Arguments: REQUEST_JSON. Run with -noanalysis against an existing project.
// @category Halftrack.Analysis
import com.google.gson.*;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.util.headless.HeadlessScript;
import ghidra.framework.Application;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.util.task.TimeoutTaskMonitor;
import java.nio.ByteBuffer;
import java.nio.channels.FileChannel;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.security.MessageDigest;
import java.time.Instant;
import java.util.*;
import java.util.concurrent.TimeUnit;

public class BulkDecompileBatch extends HeadlessScript {
    private final Gson gson = new GsonBuilder().serializeNulls().create();
    private int preparationSeconds;
    private FileChannel journal;

    @Override protected void run() throws Exception {
        if (currentProgram == null || !isRunningHeadless())
            throw new IllegalArgumentException("Existing headless program required");
        enableHeadlessAnalysis(false);
        String[] args = getScriptArgs();
        if (args.length != 1) throw new IllegalArgumentException("Expected REQUEST_JSON");
        Path requestPath = Path.of(args[0]).toAbsolutePath().normalize();
        JsonObject request = JsonParser.parseString(Files.readString(requestPath)).getAsJsonObject();
        if (request.get("schema_version").getAsInt() != 1)
            throw new IllegalArgumentException("Unsupported request version");
        String programHash = currentProgram.getExecutableSHA256();
        if (!programHash.equals(request.get("program_sha256").getAsString()))
            throw new IllegalArgumentException("Program SHA256 mismatch");
        preparationSeconds = bounded(request, "preparation_seconds", 1, 120);
        int decompileSeconds = bounded(request, "decompile_seconds", 1, 120);
        Map<String, Address> entries = new LinkedHashMap<>();
        for (JsonElement value : request.getAsJsonArray("addresses")) {
            String raw = value.getAsString();
            if (!raw.matches("0x[0-9a-f]{1,16}"))
                throw new IllegalArgumentException("Expected canonical hex address");
            Address address = currentProgram.getAddressFactory().getDefaultAddressSpace()
                .getAddress(Long.parseUnsignedLong(raw.substring(2), 16));
            if (!raw.equals(addressKey(address)) || entries.putIfAbsent(raw, address) != null)
                throw new IllegalArgumentException("Duplicate/noncanonical address");
        }
        if (entries.isEmpty() || entries.size() > 4096)
            throw new IllegalArgumentException("Batch must contain 1..4096 addresses");
        Path output = requestPath.getParent();
        Map<String, Object> meta = new LinkedHashMap<>();
        meta.put("schema_version", 1);
        meta.put("program_sha256", programHash);
        meta.put("request_sha256", sha256(Files.readAllBytes(requestPath)));
        meta.put("ghidra_version", Application.getApplicationVersion());
        meta.put("language_id", currentProgram.getLanguageID().toString());
        meta.put("compiler_spec_id", currentProgram.getCompilerSpec().getCompilerSpecID().toString());
        meta.put("broad_auto_analysis", false);
        meta.put("modeled_body_warning", "Ghidra's modeled ranges may exclude reachable indirect branch targets; not complete function boundaries");
        meta.put("requested_count", entries.size());
        meta.put("started_utc", Instant.now().toString());
        Files.writeString(output.resolve("metadata.json"), gson.toJson(meta)+"\n",
                          StandardCharsets.UTF_8, StandardOpenOption.CREATE_NEW);
        try (FileChannel channel = FileChannel.open(output.resolve("events.jsonl"),
                StandardOpenOption.CREATE_NEW, StandardOpenOption.WRITE)) {
            journal = channel;
            DecompInterface decompiler = new DecompInterface();
            int done = 0;
            try {
                if (!decompiler.openProgram(currentProgram))
                    throw new IllegalStateException(decompiler.getLastMessage());
                for (Map.Entry<String, Address> entry : entries.entrySet()) {
                    monitor.checkCancelled();
                    String key = entry.getKey();
                    event(Map.of("event", "started", "address", key));
                    Map<String,Object> result = new LinkedHashMap<>();
                    result.put("event", "result");
                    result.put("address", key);
                    result.put("status", "failed");
                    result.put("stage", "prepare");
                    result.put("timed_out", false);
                    long started = System.nanoTime();
                    try {
                        Function function = prepareFunction(entry.getValue(), result);
                        result.put("function_name", function.getName());
                        result.put("stage", "decompile");
                        decompiler.flushCache();
                        DecompileResults recovered = decompiler.decompileFunction(function, decompileSeconds, monitor);
                        if (recovered == null) throw new IllegalStateException("No decompiler result");
                        result.put("timed_out", recovered.isTimedOut());
                        result.put("cancelled", recovered.isCancelled());
                        result.put("failed_to_start", recovered.failedToStart());
                        result.put("decompiler_message", recovered.getErrorMessage());
                        recordBody(function, result);
                        if (!recovered.decompileCompleted() || recovered.getDecompiledFunction() == null)
                            throw new IllegalStateException(recovered.getErrorMessage());
                        String text = recovered.getDecompiledFunction().getC();
                        if (text == null || text.isBlank()) throw new IllegalStateException("Empty pseudocode");
                        byte[] bytes = text.getBytes(StandardCharsets.UTF_8);
                        result.put("stage", "write_output");
                        try (FileChannel code = FileChannel.open(output.resolve(key+".c"),
                                StandardOpenOption.CREATE_NEW, StandardOpenOption.WRITE)) {
                            writeAll(code, bytes);
                            code.force(true);
                        }
                        result.put("output_file", key+".c");
                        result.put("output_sha256", sha256(bytes));
                        result.put("status", "completed");
                        result.put("stage", "complete");
                    } catch (Exception error) {
                        result.put("error_type", error.getClass().getName());
                        result.put("error", error.getMessage());
                        if (Boolean.TRUE.equals(result.get("timed_out"))) result.put("status", "timed_out");
                        if (monitor.isCancelled()) result.put("status", "interrupted");
                    }
                    result.put("elapsed_ms", TimeUnit.NANOSECONDS.toMillis(System.nanoTime()-started));
                    event(result);
                    done++;
                    println("BULK " + key + " " + result.get("status"));
                }
            } finally {
                decompiler.dispose();
                event(Map.of("event", "finished", "result_count", done));
            }
        }
    }
    private static int bounded(JsonObject request, String key, int low, int high) {
        int value = request.get(key).getAsInt();
        if (value < low || value > high) throw new IllegalArgumentException("Invalid "+key);
        return value;
    }
    private void event(Map<String,?> value) throws Exception {
        writeAll(journal, (gson.toJson(value)+"\n").getBytes(StandardCharsets.UTF_8));
        journal.force(true);
    }
    private static void writeAll(FileChannel channel, byte[] bytes) throws Exception {
        ByteBuffer buffer = ByteBuffer.wrap(bytes);
        while (buffer.hasRemaining()) channel.write(buffer);
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
        TimeoutTaskMonitor preparation = TimeoutTaskMonitor.timeoutIn(preparationSeconds, TimeUnit.SECONDS);
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
        result.put("modeled_body_bytes", function.getBody().getNumAddresses());
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
        result.put("modeled_body_ranges", ranges);
        result.put("modeled_body_sha256", HexFormat.of().formatHex(digest.digest()));
        result.put("modeled_body_hash_order", "Ghidra-modeled ranges in ascending order; indirect branch targets may be absent");
    }

    private static String addressKey(Address address) {
        return "0x" + Long.toUnsignedString(address.getOffset(), 16);
    }
    private static String sha256(byte[] bytes) throws Exception {
        return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));
    }
}
