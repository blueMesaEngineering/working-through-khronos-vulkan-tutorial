#include <memory>
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#   include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif
#include <GLFW/glfw3.h>

#include <cstdlib>
#include <iostream>
#include <stdexcept>

const uint32_t WIDTH = 800;
const uint32_t HEIGHT = 600;

class HelloTriangleApplication
{
    public:


//******************************************************************************************
// 
//  Name:           run
//  Arguments:      N/A
//  Returns:        
//  Calls:          
//  Called by:      
//  Description:    Provides the next level down for the control flow of the application.
// 
//******************************************************************************************
    
        void run()
        {
            initWindow();
            initVulkan();
            mainLoop();
            cleanup();
        }

    private:
        GLFWwindow *window = nullptr;


//******************************************************************************************
// 
//  Name:           initWindow
//  Arguments:      N/A
//  Returns:        
//  Calls:          
//  Called by:      
//  Description:    Calls glfw functions to initialize a window to be displayed 
//                  on the screen.
// 
//******************************************************************************************
        
        void initWindow()
        {
            glfwInit();

            glfwWindowHint(  GLFW_CLIENT_API
                           , GLFW_NO_API);
            glfwWindowHint(  GLFW_RESIZABLE
                           , GLFW_FALSE);

            window = glfwCreateWindow(  WIDTH
                                      , HEIGHT
                                      , "Vulkan"
                                      , nullptr
                                      , nullptr);
        }


//******************************************************************************************
// 
//  Name:           initVulkan
//  Arguments:      N/A
//  Returns:        
//  Calls:          
//  Called by:      
//  Description:    Control structure for initializing the Vulkan framework.
// 
//******************************************************************************************

        void initVulkan()
        {

        }
        

//******************************************************************************************
// 
//  Name:           mainLoop
//  Arguments:      N/A
//  Returns:        
//  Calls:          
//  Called by:      
//  Description:    Checks events acted on the window (for now...).  Checks to see if 
//                  window is closed.
// 
//******************************************************************************************

        void mainLoop()
        {
            while (!glfwWindowShouldClose(window))
            {
                glfwPollEvents();
            }
        }


//******************************************************************************************
// 
//  Name:           cleanup
//  Arguments:      N/A
//  Returns:        
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void cleanup()
        {
            glfwDestroyWindow(window);

            glfwTerminate();
        }
};


//******************************************************************************************
// 
//  Name:           main
//  Arguments:      N/A
//  Returns:        
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

int main()
{
    try{
        HelloTriangleApplication app;
        app.run();
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}