#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#   include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#define GLFW_INCLUDE_VULKAN         // REQUIRED only for GLFW CreateWindowSurface.
#include <GLFW/glfw3.h>

const uint32_t WIDTH = 800;
const uint32_t HEIGHT = 600;

const std::vector<char const *> validationLayers = 
{
    "VK_LAYER_KHRONOS_validation"
};

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

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

        vk::raii::Context               context;
        vk::raii::Instance              instance        = nullptr;
        vk::raii::DebugUtilsMessengerEXT debugMessenger  = nullptr;


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
            createInstance();
            setupDebugMessenger();
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
        

//******************************************************************************************
// 
//  Name:           createInstance
//  Arguments:      N/A
//  Returns:        
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

            // Get the required layers
            std::vector<char const *> requiredLayers;
            if (enableValidationLayers)
            {
                requiredLayers.assign(  validationLayers.begin()
                                      , validationLayers.end());
            }

            // Check if the required layers are supported by the Vulkan implementation.
            auto layerProperties    = context.enumerateInstanceLayerProperties();
            auto unsupportedLayerIt = std::ranges::find_if(  requiredLayers
                                                           , [&layerProperties](auto const &requiredLayer)
                                                            {
                                                                return std::ranges::none_of(  layerProperties
                                                                                            , [requiredLayer](auto const &layerProperty)
                                                                                            {
                                                                                                return strcmp(  layerProperty.layerName
                                                                                                              , requiredLayer) == 0;
                                                                                            });
                                                            });
            if (unsupportedLayerIt != requiredLayers.end())
            {
                throw std::runtime_error("Required layer not supported: " + std::string(*unsupportedLayerIt));
            }

            // Get the required extensions.
            auto requiredExtensions = getRequiredInstanceExtensions();
            
            // Check if the required GLFW extensions are supported by the Vulkan Implementation.
            auto extensionProperties = context.enumerateInstanceExtensionProperties();
            auto unsupportedPropertyIt =
                std::ranges::find_if(  requiredExtensions
                                     , [&extensionProperties](auto const &requiredExtension)
                                     {
                                        return std::ranges::none_of(  extensionProperties
								                                    , [requiredExtension](auto const &extensionProperty)
								        {
                                            return strcmp(  extensionProperty.extensionName
                                                          , requiredExtension) == 0;
                                        });
                                     });
            if (unsupportedPropertyIt != requiredExtensions.end())
            {
                throw std::runtime_error("Required extension not supported: " + std::string(*unsupportedPropertyIt));
            }            
            
            vk::InstanceCreateInfo createInfo
            {
                  .pApplicationInfo         = &appInfo
                , .enabledLayerCount        = static_cast<uint32_t>(requiredLayers.size())
                , .ppEnabledLayerNames      = requiredLayers.data()
                , .enabledExtensionCount    = static_cast<uint32_t>(requiredExtensions.size())
                , .ppEnabledExtensionNames  = requiredExtensions.data()
            };
            
            instance = vk::raii::Instance(  context
                                          , createInfo);
        }
        

//******************************************************************************************
// 
//  Name:           setupDebugMessenger
//  Arguments:      N/A
//  Returns:        
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************
                    
        void setupDebugMessenger()
        {
            if (!enableValidationLayers)
                return;

            vk::DebugUtilsMessageSeverityFlagsEXT   severityFlags(vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                                                                  vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
            vk::DebugUtilsMessageTypeFlagsEXT       messageTypeFlags(vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                                                                     vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
                                                                     vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);
            vk::DebugUtilsMessengerCreateInfoEXT    debugUtilsMessengerCreateInfoEXT
            {
                  .messageSeverity  = severityFlags
                , .messageType      = messageTypeFlags
                , .pfnUserCallback  = &debugCallback
            };
            debugMessenger = instance.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfoEXT);        
        }
        

//******************************************************************************************
// 
//  Name:           getRequiredInstanceExtensions
//  Arguments:      N/A
//  Returns:        
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************
        
        std::vector<const char *> getRequiredInstanceExtensions()
        {
            uint32_t    glfwExtensionCount = 0;
            auto        glfwExtensions     = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

            std::vector extensions(  glfwExtensions
                                   , glfwExtensions + glfwExtensionCount);
            if (enableValidationLayers)
            {
                extensions.push_back(vk::EXTDebugUtilsExtensionName);
            }

            return extensions;
        }
        

//******************************************************************************************
// 
//  Name:           getRequiredInstanceExtensions
//  Arguments:      N/A
//  Returns:        
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(  vk::DebugUtilsMessageSeverityFlagBitsEXT severity
                                                              , vk::DebugUtilsMessageTypeFlagsEXT type
                                                              , const vk::DebugUtilsMessengerCallbackDataEXT *pCallbackData
                                                              , void *)
        {
            if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError ||
                severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning)
            {
                std::cerr << "Validation layer: type " << to_string(type) << " msg: " << pCallbackData->pMessage << std::endl;
            }

            return vk::False;
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