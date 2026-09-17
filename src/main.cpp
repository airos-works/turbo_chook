#include "ui/Application.h"
#include <signal.h>
int main() {
    signal(SIGPIPE, SIG_IGN);
    tc::Application app;
    app.Run();
    return 0;
}
