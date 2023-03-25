#include <iostream>
#include <application.hpp>

using namespace uva;
using namespace routing;
using namespace console;

#include <application_controller.hpp>

DECLARE_CONSOLE_APPLICATION(
    //Declare your routes above. As good practice, keep then ordered by controler.
    //You can have C++ code here, perfect for init other libraries.

    //'index' route
    ROUTE("", application_controller::run);


    ROUTE("run", application_controller::run);
)
