#include <QApplication>

#include "src/ui/PetWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("QtDesktopPet");
    QApplication::setOrganizationName("QtDesktopPet");

    PetWindow window;
    window.show();

    return app.exec();
}

