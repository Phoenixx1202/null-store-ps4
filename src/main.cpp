// NULL_store - PS4 entry point.
#include <orbis/SystemService.h>
#include <orbis/libkernel.h>

#include "app/Application.h"

int main()
{
    ns::Application app;
    const int result = app.run(); // only returns when start-up failed
    // Give the log a moment to reach storage, then ask the system to close the
    // title: returning from main is not a clean exit for this kind of title.
    sceKernelUsleep(200000);
    sceSystemServiceLoadExec("exit", nullptr);
    return result;
}
