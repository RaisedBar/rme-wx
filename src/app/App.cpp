#include "App.h"

#include "MainFrame.h"

wxIMPLEMENT_APP(App);

bool App::OnInit() {
    if (!wxApp::OnInit()) return false;
    SetAppName("TotalMixAccess");
    SetVendorName("TotalMixAccess");
#if defined(__WXMSW__) && wxCHECK_VERSION(3, 3, 0)
    MSWEnableDarkMode();  // macOS / GTK follow the system appearance automatically
#endif
    auto* frame = new MainFrame();
    frame->Show();
    return true;
}
