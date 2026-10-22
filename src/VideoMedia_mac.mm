#include "VideoMedia.h"
#include <juce_audio_formats/juce_audio_formats.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

// AVFoundation's synchronous asset properties still answer on every supported macOS; the
// asynchronous loaders would need a run loop the callers do not have.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

namespace halftrack::video
{
namespace
{
NSURL* urlOf (const juce::File& file) { return [NSURL fileURLWithPath: [NSString stringWithUTF8String: file.getFullPathName().toRawUTF8()]]; }
juce::String describe (NSError* error) { return error != nil ? juce::String ([[error localizedDescription] UTF8String]) : juce::String ("unknown error"); }
AVURLAsset* assetOf (const juce::File& file)
{
    return [AVURLAsset URLAssetWithURL: urlOf (file) options: @{ AVURLAssetPreferPreciseDurationAndTimingKey: @YES }];
}
// Waits for an asynchronous AVFoundation job without a run loop.
template <typename Start> void waitFor (Start&& start)
{
    dispatch_semaphore_t done = dispatch_semaphore_create (0);
    start (done);
    dispatch_semaphore_wait (done, DISPATCH_TIME_FOREVER);
    dispatch_release (done);
}
}

bool supported() { return true; }
bool isMovie (const juce::File& file) { return file.hasFileExtension ("mov;mp4;m4v"); }

juce::Result probe (const juce::File& movie, Info& info)
{
    @autoreleasepool
    {
        if (! movie.existsAsFile()) return juce::Result::fail ("The movie does not exist.");
        auto* asset = assetOf (movie);
        NSArray* pictures = [asset tracksWithMediaType: AVMediaTypeVideo];
        if (pictures.count == 0) return juce::Result::fail ("This file has no picture to show.");
        AVAssetTrack* picture = pictures[0];
        info.seconds = CMTimeGetSeconds (asset.duration);
        info.frameRate = picture.nominalFrameRate;
        const auto size = CGSizeApplyAffineTransform (picture.naturalSize, picture.preferredTransform);
        info.width = (int) std::abs (size.width); info.height = (int) std::abs (size.height);
        info.hasSound = [asset tracksWithMediaType: AVMediaTypeAudio].count > 0;
        if (! (info.seconds > 0)) return juce::Result::fail ("The movie has no length.");
        return juce::Result::ok();
    }
}

juce::Result extractSound (const juce::File& movie, const juce::File& wav)
{
    @autoreleasepool
    {
        Info info;
        if (auto r = probe (movie, info); r.failed()) return r;
        juce::WavAudioFormat format;
        auto* asset = assetOf (movie);
        NSArray* sounds = [asset tracksWithMediaType: AVMediaTypeAudio];
        if (sounds.count == 0)
        {
            // A silent movie still gets its length of sound, so its clip lasts as long as its picture.
            std::unique_ptr<juce::OutputStream> stream (wav.createOutputStream());
            auto writer = stream ? format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (24)) : nullptr;
            if (writer == nullptr) return juce::Result::fail ("Could not write the movie's sound.");
            juce::AudioBuffer<float> silence (2, 48000);
            silence.clear();
            for (auto left = (juce::int64) std::llround (info.seconds * 48000); left > 0; left -= silence.getNumSamples())
                if (! writer->writeFromAudioSampleBuffer (silence, 0, (int) std::min<juce::int64> (left, silence.getNumSamples())))
                    return juce::Result::fail ("Could not write the movie's sound.");
            return juce::Result::ok();
        }
        AVAssetTrack* sound = sounds[0];
        double rate = 48000;
        int channels = 2;
        if (sound.formatDescriptions.count > 0)
            if (const auto* description = CMAudioFormatDescriptionGetStreamBasicDescription ((CMAudioFormatDescriptionRef) sound.formatDescriptions[0]))
            {
                if (description->mSampleRate > 0) rate = description->mSampleRate;
                channels = juce::jlimit (1, 2, (int) description->mChannelsPerFrame);
            }
        NSError* error = nil;
        AVAssetReader* reader = [AVAssetReader assetReaderWithAsset: asset error: &error];
        if (reader == nil) return juce::Result::fail ("Could not read the movie's sound: " + describe (error));
        NSDictionary* settings = @{ AVFormatIDKey: @(kAudioFormatLinearPCM), AVLinearPCMBitDepthKey: @32, AVLinearPCMIsFloatKey: @YES,
                                    AVLinearPCMIsNonInterleaved: @NO, AVLinearPCMIsBigEndianKey: @NO, AVSampleRateKey: @(rate), AVNumberOfChannelsKey: @(channels) };
        AVAssetReaderTrackOutput* output = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack: sound outputSettings: settings];
        [reader addOutput: output];
        if (! [reader startReading]) return juce::Result::fail ("Could not read the movie's sound: " + describe (reader.error));
        std::unique_ptr<juce::OutputStream> stream (wav.createOutputStream());
        auto writer = stream ? format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (channels).withBitsPerSample (24)) : nullptr;
        if (writer == nullptr) return juce::Result::fail ("Could not write the movie's sound.");
        std::vector<float> interleaved;
        juce::AudioBuffer<float> block;
        while (CMSampleBufferRef buffer = [output copyNextSampleBuffer])
        {
            const auto bytes = CMBlockBufferGetDataLength (CMSampleBufferGetDataBuffer (buffer));
            interleaved.resize (bytes / sizeof (float));
            CMBlockBufferCopyDataBytes (CMSampleBufferGetDataBuffer (buffer), 0, bytes, interleaved.data());
            CFRelease (buffer);
            const int frames = (int) (interleaved.size() / (size_t) channels);
            block.setSize (channels, frames, false, false, true);
            for (int c = 0; c < channels; ++c)
                for (int n = 0; n < frames; ++n) block.setSample (c, n, interleaved[(size_t) (n * channels + c)]);
            if (! writer->writeFromAudioSampleBuffer (block, 0, frames)) return juce::Result::fail ("Could not write the movie's sound.");
        }
        if (reader.status == AVAssetReaderStatusFailed) return juce::Result::fail ("Could not read the movie's sound: " + describe (reader.error));
        return juce::Result::ok();
    }
}

juce::Result writeWithSound (const juce::File& movie, double from, double seconds, const juce::File& soundFile, const juce::File& output)
{
    @autoreleasepool
    {
        Info info;
        if (auto r = probe (movie, info); r.failed()) return r;
        if (! (from >= 0 && from < info.seconds && seconds > 0)) return juce::Result::fail ("The movie does not reach the exported time.");
        auto* picture = assetOf (movie);
        auto* sound = assetOf (soundFile);
        NSArray* soundTracks = [sound tracksWithMediaType: AVMediaTypeAudio];
        if (soundTracks.count == 0) return juce::Result::fail ("The exported sound could not be read.");
        AVMutableComposition* composition = [AVMutableComposition composition];
        AVAssetTrack* source = [picture tracksWithMediaType: AVMediaTypeVideo][0];
        AVMutableCompositionTrack* video = [composition addMutableTrackWithMediaType: AVMediaTypeVideo preferredTrackID: kCMPersistentTrackID_Invalid];
        NSError* error = nil;
        const auto span = CMTimeRangeMake (CMTimeMakeWithSeconds (from, 60000), CMTimeMakeWithSeconds (std::min (seconds, info.seconds - from), 60000));
        if (! [video insertTimeRange: span ofTrack: source atTime: kCMTimeZero error: &error]) return juce::Result::fail ("Could not take the movie's picture: " + describe (error));
        video.preferredTransform = source.preferredTransform;
        AVMutableCompositionTrack* audio = [composition addMutableTrackWithMediaType: AVMediaTypeAudio preferredTrackID: kCMPersistentTrackID_Invalid];
        AVAssetTrack* mix = soundTracks[0];
        if (! [audio insertTimeRange: CMTimeRangeMake (kCMTimeZero, sound.duration) ofTrack: mix atTime: kCMTimeZero error: &error])
            return juce::Result::fail ("Could not add the exported sound: " + describe (error));
        output.deleteFile();
        AVAssetExportSession* session = [[AVAssetExportSession alloc] initWithAsset: composition presetName: AVAssetExportPresetPassthrough];
        session.outputURL = urlOf (output);
        session.outputFileType = AVFileTypeQuickTimeMovie;
        waitFor ([&] (dispatch_semaphore_t done) { [session exportAsynchronouslyWithCompletionHandler: ^{ dispatch_semaphore_signal (done); }]; });
        const bool written = session.status == AVAssetExportSessionStatusCompleted;
        const auto reason = describe (session.error);
        [session release];
        return written ? juce::Result::ok() : juce::Result::fail ("Could not write the movie: " + reason);
    }
}

juce::Result writeTestMovie (const juce::File& movie, int frames, double frameRate, int width, int height, double toneHz, double sampleRate)
{
    @autoreleasepool
    {
        movie.deleteFile();
        NSError* error = nil;
        AVAssetWriter* writer = [AVAssetWriter assetWriterWithURL: urlOf (movie) fileType: AVFileTypeQuickTimeMovie error: &error];
        if (writer == nil) return juce::Result::fail ("Could not start the test movie: " + describe (error));
        // Photo JPEG: every frame stands alone, so each is exactly where it was written.
        AVAssetWriterInput* pictures = [AVAssetWriterInput assetWriterInputWithMediaType: AVMediaTypeVideo
                                                                          outputSettings: @{ AVVideoCodecKey: AVVideoCodecTypeJPEG, AVVideoWidthKey: @(width), AVVideoHeightKey: @(height) }];
        pictures.expectsMediaDataInRealTime = NO;
        AVAssetWriterInputPixelBufferAdaptor* adaptor = [AVAssetWriterInputPixelBufferAdaptor
            assetWriterInputPixelBufferAdaptorWithAssetWriterInput: pictures
                                       sourcePixelBufferAttributes: @{ (NSString*) kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
                                                                       (NSString*) kCVPixelBufferWidthKey: @(width), (NSString*) kCVPixelBufferHeightKey: @(height) }];
        AudioChannelLayout layout {};
        layout.mChannelLayoutTag = kAudioChannelLayoutTag_Stereo;
        AVAssetWriterInput* sound = [AVAssetWriterInput assetWriterInputWithMediaType: AVMediaTypeAudio
            outputSettings: @{ AVFormatIDKey: @(kAudioFormatLinearPCM), AVSampleRateKey: @(sampleRate), AVNumberOfChannelsKey: @2,
                               AVLinearPCMBitDepthKey: @16, AVLinearPCMIsFloatKey: @NO, AVLinearPCMIsBigEndianKey: @NO, AVLinearPCMIsNonInterleaved: @NO,
                               AVChannelLayoutKey: [NSData dataWithBytes: &layout length: sizeof (layout)] }];
        sound.expectsMediaDataInRealTime = NO;
        if (! [writer canAddInput: pictures] || ! [writer canAddInput: sound]) return juce::Result::fail ("The test movie cannot hold its picture and sound.");
        [writer addInput: pictures]; [writer addInput: sound];
        if (! [writer startWriting]) return juce::Result::fail ("Could not start the test movie: " + describe (writer.error));
        [writer startSessionAtSourceTime: kCMTimeZero];

        AudioStreamBasicDescription pcm {};
        pcm.mSampleRate = sampleRate; pcm.mFormatID = kAudioFormatLinearPCM; pcm.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
        pcm.mBytesPerPacket = 4; pcm.mFramesPerPacket = 1; pcm.mBytesPerFrame = 4; pcm.mChannelsPerFrame = 2; pcm.mBitsPerChannel = 16;
        CMAudioFormatDescriptionRef soundFormat = nullptr;
        CMAudioFormatDescriptionCreate (kCFAllocatorDefault, &pcm, sizeof (layout), &layout, 0, nullptr, nullptr, &soundFormat);
        const auto totalFrames = (juce::int64) std::llround (frames / frameRate * sampleRate);
        juce::int64 soundWritten = 0;
        bool soundFinished = false;
        const auto appendSound = [&] (juce::int64 upTo)
        {
            while (soundWritten < upTo && soundWritten < totalFrames)
            {
                if (! sound.readyForMoreMediaData) return;
                const int count = (int) std::min<juce::int64> (4800, std::min (upTo, totalFrames) - soundWritten);
                CMBlockBufferRef block = nullptr;
                CMBlockBufferCreateWithMemoryBlock (kCFAllocatorDefault, nullptr, (size_t) count * 4, kCFAllocatorDefault, nullptr, 0, (size_t) count * 4, 0, &block);
                std::vector<int16_t> samples ((size_t) count * 2);
                for (int n = 0; n < count; ++n)
                    samples[(size_t) n * 2] = samples[(size_t) n * 2 + 1] = (int16_t) std::lround (0.5 * 32767 * std::sin (juce::MathConstants<double>::twoPi * toneHz * (double) (soundWritten + n) / sampleRate));
                CMBlockBufferReplaceDataBytes (samples.data(), block, 0, (size_t) count * 4);
                CMSampleBufferRef buffer = nullptr;
                CMAudioSampleBufferCreateReadyWithPacketDescriptions (kCFAllocatorDefault, block, soundFormat, count, CMTimeMake (soundWritten, (int32_t) sampleRate), nullptr, &buffer);
                [sound appendSampleBuffer: buffer];
                CFRelease (buffer); CFRelease (block);
                soundWritten += count;
            }
        };
        for (int frame = 0; frame < frames; ++frame)
        {
            // The two inputs interleave: while the picture waits, the sound may run on ahead, and once
            // all of it is in, the writer must hear that there is no more.
            while (! pictures.readyForMoreMediaData)
            {
                appendSound (totalFrames);
                if (soundWritten >= totalFrames && ! soundFinished) { [sound markAsFinished]; soundFinished = true; }
                juce::Thread::sleep (1);
            }
            CVPixelBufferRef pixels = nullptr;
            CVPixelBufferPoolCreatePixelBuffer (kCFAllocatorDefault, adaptor.pixelBufferPool, &pixels);
            if (pixels == nullptr) { CFRelease (soundFormat); return juce::Result::fail ("Could not make a test frame."); }
            CVPixelBufferLockBaseAddress (pixels, 0);
            auto* base = (juce::uint8*) CVPixelBufferGetBaseAddress (pixels);
            const auto stride = CVPixelBufferGetBytesPerRow (pixels);
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                {
                    const int bit = juce::jmin (5, x * 6 / width);
                    const juce::uint8 level = (frame >> bit) & 1 ? 235 : 20;
                    auto* p = base + (size_t) y * stride + (size_t) x * 4;
                    p[0] = p[1] = p[2] = level; p[3] = 255;
                }
            CVPixelBufferUnlockBaseAddress (pixels, 0);
            [adaptor appendPixelBuffer: pixels withPresentationTime: CMTimeMakeWithSeconds (frame / frameRate, 60000)];
            CVPixelBufferRelease (pixels);
            if (! soundFinished) appendSound ((juce::int64) ((frame + 12) / frameRate * sampleRate));
        }
        [pictures markAsFinished];
        while (soundWritten < totalFrames) { appendSound (totalFrames); if (soundWritten < totalFrames) juce::Thread::sleep (1); }
        if (! soundFinished) [sound markAsFinished];
        CFRelease (soundFormat);
        waitFor ([&] (dispatch_semaphore_t done) { [writer finishWritingWithCompletionHandler: ^{ dispatch_semaphore_signal (done); }]; });
        return writer.status == AVAssetWriterStatusCompleted ? juce::Result::ok() : juce::Result::fail ("Could not finish the test movie: " + describe (writer.error));
    }
}

juce::Image frameImage (const juce::File& movie, double seconds, int height)
{
    @autoreleasepool
    {
        if (height <= 0) return {};
        AVAssetImageGenerator* generator = [AVAssetImageGenerator assetImageGeneratorWithAsset: assetOf (movie)];
        generator.appliesPreferredTrackTransform = YES;
        generator.maximumSize = CGSizeMake (height * 8, height);
        generator.requestedTimeToleranceBefore = CMTimeMakeWithSeconds (0.1, 600);
        generator.requestedTimeToleranceAfter = CMTimeMakeWithSeconds (0.1, 600);
        CGImageRef picture = [generator copyCGImageAtTime: CMTimeMakeWithSeconds (std::max (0.0, seconds), 60000) actualTime: nil error: nil];
        if (picture == nullptr) return {};
        const auto sourceWidth = (double) CGImageGetWidth (picture), sourceHeight = (double) CGImageGetHeight (picture);
        const auto width = juce::jmax (1, (int) std::lround (height * sourceWidth / std::max (1.0, sourceHeight)));
        juce::Image image (juce::Image::ARGB, width, height, true);
        {
            // JUCE's ARGB pixels are little-endian premultiplied BGRA, as this context writes them.
            juce::Image::BitmapData pixels (image, juce::Image::BitmapData::writeOnly);
            CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
            CGContextRef context = CGBitmapContextCreate (pixels.data, (size_t) width, (size_t) height, 8, (size_t) pixels.lineStride, space,
                                                          (CGBitmapInfo) kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
            CGContextDrawImage (context, CGRectMake (0, 0, width, height), picture);
            CGContextRelease (context); CGColorSpaceRelease (space);
        }
        CGImageRelease (picture);
        return image;
    }
}

juce::Result testFrameAt (const juce::File& movie, double seconds, int& frame)
{
    @autoreleasepool
    {
        AVAssetImageGenerator* generator = [AVAssetImageGenerator assetImageGeneratorWithAsset: assetOf (movie)];
        generator.requestedTimeToleranceBefore = kCMTimeZero;
        generator.requestedTimeToleranceAfter = kCMTimeZero;
        NSError* error = nil;
        CGImageRef image = [generator copyCGImageAtTime: CMTimeMakeWithSeconds (seconds, 60000) actualTime: nil error: &error];
        if (image == nullptr) return juce::Result::fail ("Could not read the movie's frame: " + describe (error));
        const auto width = (int) CGImageGetWidth (image), height = (int) CGImageGetHeight (image);
        std::vector<juce::uint8> pixels ((size_t) width * (size_t) height * 4);
        CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
        CGContextRef context = CGBitmapContextCreate (pixels.data(), (size_t) width, (size_t) height, 8, (size_t) width * 4, space, kCGImageAlphaPremultipliedLast);
        CGContextDrawImage (context, CGRectMake (0, 0, width, height), image);
        CGContextRelease (context); CGColorSpaceRelease (space); CGImageRelease (image);
        // Each stripe's centre, along the middle row, bright or dark.
        frame = 0;
        for (int bit = 0; bit < 6; ++bit)
        {
            const auto x = (int) ((bit + 0.5) * width / 6);
            const auto* p = pixels.data() + ((size_t) (height / 2) * (size_t) width + (size_t) x) * 4;
            if ((p[0] + p[1] + p[2]) / 3 > 128) frame |= 1 << bit;
        }
        return juce::Result::ok();
    }
}
}

#pragma clang diagnostic pop
