#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#   include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#define GLFW_INCLUDE_VULKAN         // REQUIRED only for GLFW CreateWindowSurface.
#include <GLFW/glfw3.h>

const uint32_t WIDTH = 800;
const uint32_t HEIGHT = 600;

class HelloTriangleApplication
{
    public:

//******************************************************************************************
// 
//  Name:           run
//  Arguments:      N/A
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

        vk::raii::Context  context;
        vk::raii::Instance instance = nullptr;


//******************************************************************************************
// 
//  Name:           initWindow
//  Arguments:      N/A
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
//  Calls:          
//  Called by:      
//  Description:    Control structure for initializing the Vulkan framework.
// 
//******************************************************************************************

        void initVulkan()
        {
            createInstance();
        }
        

//******************************************************************************************
// 
//  Name:           mainLoop
//  Arguments:      N/A
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
        

//******************************************************************************************
// 
//  Name:           createInstance
//  Arguments:      N/A
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void createInstance()
        {
            constexpr vk::ApplicationInfo appInfo
            {
                  .pApplicationName     = "Hello Triangle"
                , .applicationVersion   = VK_MAKE_VERSION(1, 0, 0)
                , .pEngineName          = "No Engine"
                , .engineVersion        = VK_MAKE_VERSION(1, 0, 0)
                , .apiVersion           = vk::ApiVersion14
            };

            // Get the required instance extensions from GLFW.
            uint32_t glfwExtensionCount = 0;
            auto glfwExtensions         = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

            // Check if the required FLDW extensions are supported by the Vulkan Implementation.
            auto extensionProperties = context.enumerateInstanceExtensionProperties();
            for (uint32_t i = 0; i < glfwExtensionCount; ++i)
            {
                if (std::ranges::none_of(  extensionProperties
                                         , [glfwExtension = glfwExtensions[i]](auto const &extensionProperty)
                                           {
                                                return strcmp(  extensionProperty.extensionName
                                                              , glfwExtension) == 0;
                                            }))
                {
                    throw std::runtime_error("Required GLFW extension not supported: " + std::string(glfwExtensions[i]));
                }
            }

            vk::InstanceCreateInfo createInfo
            {
                  .pApplicationInfo         = &appInfo
                , .enabledExtensionCount    = glfwExtensionCount
                , .ppEnabledExtensionNames  = glfwExtensions
            };

            instance = vk::raii::Instance(  context
                                          , createInfo);
        }
};
        

//******************************************************************************************
// 
//  Name:           main
//  Arguments:      N/A
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

int main()
{
    try
    {
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