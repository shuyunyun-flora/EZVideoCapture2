#include "EZVideoCaptureWindow.h"
#include <QtWidgets/QApplication>
#include <mfapi.h>
#include <objbase.h>

int main(int argc, char *argv[])
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr))
        return -1;

    hr = MFStartup(MF_VERSION);
    if (FAILED(hr))
    {
        CoUninitialize();
        return -1;
    }

    QApplication app(argc, argv);

	EZVideoCaptureWindow w;
    w.resize(650, 500);
    w.show();

    int ret = app.exec();

    MFShutdown();
    CoUninitialize();

    return ret;
}
