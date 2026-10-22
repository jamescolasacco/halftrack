#include "InputPermission.h"
#include <juce_events/juce_events.h>
#import <AVFoundation/AVFoundation.h>

// macOS keeps audio input permission per application (System Settings, Privacy & Security, Microphone).
namespace halftrack::inputpermission
{
Status status()
{
    switch ([AVCaptureDevice authorizationStatusForMediaType: AVMediaTypeAudio])
    {
        case AVAuthorizationStatusAuthorized: return Status::granted;
        case AVAuthorizationStatusNotDetermined: return Status::undetermined;
        case AVAuthorizationStatusDenied:
        case AVAuthorizationStatusRestricted:
        default: return Status::denied;
    }
}
void request (std::function<void (bool)> done)
{
    [AVCaptureDevice requestAccessForMediaType: AVMediaTypeAudio completionHandler: ^(BOOL granted)
    {
        const bool allowed = granted == YES;
        juce::MessageManager::callAsync ([done, allowed] { if (done) done (allowed); });
    }];
}
}
