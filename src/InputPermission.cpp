#include "InputPermission.h"

// Systems without a permission of their own for audio inputs.
namespace halftrack::inputpermission
{
Status status() { return Status::granted; }
void request (std::function<void (bool)> done) { if (done) done (true); }
}
