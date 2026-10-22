#pragma once

#include <functional>

namespace halftrack::inputpermission
{
// The system's permission to use audio inputs: macOS asks once and keeps the answer in System
// Settings; other systems grant it here and refuse, if at all, when a device opens.
enum class Status { granted, undetermined, denied };
Status status();
// Shows the system's prompt for an undetermined permission; done runs on the message thread.
void request (std::function<void (bool granted)> done);
}
