#include <algorithm>
#include <assert.h>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
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

constexpr uint32_t                  WIDTH                   = 800;
constexpr uint32_t                  HEIGHT                  = 600;
constexpr int                       MAX_FRAMES_IN_FLIGHT    = 2;

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
//  Returns:        void
//  Calls:          initWindow
//                  initVulkan
//                  mainLoop
//                  cleanup
//  Called by:      main
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
        GLFWwindow                              *window                     = nullptr;
        vk::raii::Context                       context;
        vk::raii::Instance                      instance                    = nullptr;
        vk::raii::DebugUtilsMessengerEXT        debugMessenger              = nullptr;
        vk::raii::SurfaceKHR                    surface                     = nullptr;
        vk::raii::PhysicalDevice                physicalDevice              = nullptr;
        vk::raii::Device                        device                      = nullptr;
        uint32_t                                queueIndex                  = ~0;
        vk::raii::Queue                         queue                       = nullptr;
        vk::raii::SwapchainKHR                  swapChain                   = nullptr;
        std::vector<vk::Image>                  swapChainImages;
        vk::SurfaceFormatKHR                    swapChainSurfaceFormat;
        vk::Extent2D                            swapChainExtent;
        std::vector<vk::raii::ImageView>        swapChainImageViews;

        vk::raii::PipelineLayout                pipelineLayout              = nullptr;
        vk::raii::Pipeline                      graphicsPipeline            = nullptr;

        vk::raii::CommandPool                   commandPool                 = nullptr;
        std::vector<vk::raii::CommandBuffer>    commandBuffers;

        std::vector<vk::raii::Semaphore>        presentCompleteSemaphores;
        std::vector<vk::raii::Semaphore>        renderFinishedSemaphores;
        std::vector<vk::raii::Fence>            inFlightFences;
        uint32_t                                frameIndex                  = 0;

        bool framebufferResized                                             = false;

        std::vector<const char *>               requiredDeviceExtension     =
        {
            vk::KHRSwapchainExtensionName
        };


//******************************************************************************************
// 
//  Name:           initWindow
//  Arguments:      N/A
//  Returns:        void
//  Calls:          glfwInit
//                  glfwWindowHint
//                  glfwWindowHint
//                  glfwCreateWindow
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
                           , GLFW_TRUE);

            window = glfwCreateWindow(  WIDTH
                                      , HEIGHT
                                      , "Vulkan"
                                      , nullptr
                                      , nullptr);

            glfwSetWindowUserPointer(  window
                                     , this);
            glfwSetFramebufferSizeCallback(  window
                                           , framebufferResizeCallback);
        }
        

//******************************************************************************************
// 
//  Name:           framebufferResizeCallback
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        static void framebufferResizeCallback(  GLFWwindow *window
                                              , int width
                                              , int height)
        {
            auto app                = reinterpret_cast<HelloTriangleApplication *>(glfwGetWindowUserPointer(window));
            app->framebufferResized = true;
        }


//******************************************************************************************
// 
//  Name:           initVulkan
//  Arguments:      N/A
//  Returns:        void
//  Calls:          createInstance
//                  setupDebugMessenger
//                  createSurface
//                  pickPhysicalDevice
//                  createLogicalDevice
//                  createSwapChain
//                  createImageViews
//  Called by:      initVulkan
//  Description:    Control structure for initializing the Vulkan framework.
// 
//******************************************************************************************

        void initVulkan()
        {
            createInstance();
            setupDebugMessenger();
            createSurface();
            pickPhysicalDevice();
            createLogicalDevice();
            createSwapChain();
            createImageViews();
            createGraphicsPipeline();
            createCommandPool();
            createCommandBuffers();
            createSyncObjects();
        }
        

//******************************************************************************************
// 
//  Name:           mainLoop
//  Arguments:      N/A
//  Returns:        void
//  Calls:          glfwWindowShouldClose
//                  glfwPollEvents
//  Called by:      run
//  Description:    Checks events acted on the window (for now...).  Checks to see if 
//                  window is closed.
// 
//******************************************************************************************

        void mainLoop()
        {
            while (!glfwWindowShouldClose(window))
            {
                glfwPollEvents();
                drawFrame();
            }
            device.waitIdle();      // Wait for device to finish operations before destroying resources
        }
        

//******************************************************************************************
// 
//  Name:           cleanupSwapChain
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************
        
        void cleanupSwapChain()
        {
            swapChainImageViews.clear();
            swapChain = nullptr;
        }


//******************************************************************************************
// 
//  Name:           cleanup
//  Arguments:      N/A
//  Returns:        void
//  Calls:          glfwDestroyWindow
//                  glfwTerminate
//  Called by:      run
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
//  Name:           recreateSwapChain
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void recreateSwapChain()
        {
            int   width         = 0
                , height        = 0;
            
            glfwGetFramebufferSize(  window
                                   , &width
                                   , &height);

            while (width == 0 || height == 0)
            {
                glfwGetFramebufferSize(  window
                                       , &width
                                       , &height);
                glfwWaitEvents();
            }

            device.waitIdle();

            cleanupSwapChain();
            createSwapChain();
            createImageViews();
        }
        

//******************************************************************************************
// 
//  Name:           createInstance
//  Arguments:      N/A
//  Returns:        void
//  Calls:          getRequiredInstanceExtensions
//                  context.enumerateInstanceExtensionProperties
//                  vk::raii::Instance
//  Called by:      initVulkan
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
//  Returns:        void
//  Calls:          instance.createDebugUtilsMessengerEXT
//  Called by:      initVulkan
//  Description:    If validation layers are enabled, declares createInfo so that the
//                  debug messenger can be initialized for debugging.
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
//  Name:           createSurface
//  Arguments:      N/A
//  Returns:        void
//  Calls:          glfwCreateWindowSurface
//                  SurfaceKHR
//  Called by:      initVulkan
//  Description:    Must be an OpenGL thing. An OpenGL window needs a surface to draw on.
//                  Or some such.
// 
//******************************************************************************************

        void createSurface()
        {
            VkSurfaceKHR _surface;
            if (glfwCreateWindowSurface(  *instance
                                        , window
                                        , nullptr
                                        , &_surface) != 0)
            {
                throw std::runtime_error("Failed to create window surface!");
            }
            surface = vk::raii::SurfaceKHR(  instance
                                           , _surface);
        }
        

//******************************************************************************************
// 
//  Name:           isDeviceSuitable
//  Arguments:      vk::raii::PhysicalDevice const &physicalDevice
//  Returns:        bool
//  Calls:          physicalDevice.getProperties().apiVersion
//                  physicalDevice.getQueueFamilyProperties
//                  physicalDevice.enumerateDeviceExtensionProperties
//                  get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters
//                  get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering
//                  get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState
//  Called by:      pickPhysicalDevice
//  Description:    Digs into the parameters of physicalDevice.  Checks to see if device 
//                      features match or exceed requirements for use by the API and the
//                      pipeline.
// 
//******************************************************************************************

        bool isDeviceSuitable(vk::raii::PhysicalDevice const &physicalDevice)
        {
            // Check if the physicalDevice supports the Vulkan 1.3 API version
            bool supportsVulkan1_3 = physicalDevice.getProperties().apiVersion >= VK_API_VERSION_1_3;

            // Check if any of the queue families support graphics operations
            auto queueFamilies      = physicalDevice.getQueueFamilyProperties();
            bool supportsGraphics   = std::ranges::any_of(  queueFamilies
                                                          , [](auto const &qfp)
                                                        {
                                                            return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics);
                                                        });

            // Check if all required physicalDevice extensions are available
            auto availableDeviceExtensions = physicalDevice.enumerateDeviceExtensionProperties();
            bool supportsAllRequiredExtensions = 
                std::ranges::all_of(  requiredDeviceExtension
                                    , [&availableDeviceExtensions](auto const &requiredDeviceExtension)
                                    {
                                        return std::ranges::any_of(  availableDeviceExtensions
                                                                   , [requiredDeviceExtension](auto const &availableDeviceExtension)
                                                                    {
                                                                        return strcmp(  availableDeviceExtension.extensionName
                                                                                      , requiredDeviceExtension) == 0;
                                                                    });
                                    });

            // Check if the physicalDevice supports the required features
            auto features                   = physicalDevice.template getFeatures2<  vk::PhysicalDeviceFeatures2
                                                                                   , vk::PhysicalDeviceVulkan11Features
                                                                                   , vk::PhysicalDeviceVulkan13Features
                                                                                   , vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
            bool supportsRequiredFeatures   = features.template get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters &&
                                              features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
                                              features.template get<vk::PhysicalDeviceVulkan13Features>().synchronization2 &&
                                              features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState;
                                              
            // Return true if the physicalDevice meets all the criteria
            return supportsVulkan1_3 && supportsGraphics && supportsAllRequiredExtensions && supportsRequiredFeatures;
        }
        

//******************************************************************************************
// 
//  Name:           pickPhysicalDevice
//  Arguments:      N/A
//  Returns:        void
//  Calls:          instance.enumeratePhysicalDevices
//                  isDeviceSuitable
//  Called by:      initVulkan
//  Description:    Searches for devices (GPUs) in order to find a suitable physical device
//                      with which to interface via the API.
// 
//******************************************************************************************

        void pickPhysicalDevice()
        {
            std::vector<vk::raii::PhysicalDevice>   physicalDevices = instance.enumeratePhysicalDevices();
            auto const                              devIter         = std::ranges::find_if(  physicalDevices
                                                                                           , [&](auto const &physicalDevice)
                                                                                            {
                                                                                                return isDeviceSuitable(physicalDevice);
                                                                                            });
            if (devIter == physicalDevices.end())
            {
                throw std::runtime_error("Failed to find a suitable GPU!");
            }
            physicalDevice = *devIter;
        }
        

//******************************************************************************************
// 
//  Name:           createLogicalDevice
//  Arguments:      N/A
//  Returns:        void
//  Calls:          physicalDevice.getQueueFamilyProperties
//                  physicalDevice.getSurfaceSupportKHR
//                  vk::raii::Device
//                  vk::raii::Queue
//  Called by:      initVulkan
//  Description:    Since a logical device can be different from a physical device (a 
//                      logical device can be comprised of multiple physical devices,
//                      if I am not mistaken), the API can be used to interface with
//                      multiple physical devices, so all these system devices should be 
//                      checked to see if they are compatible with the API, especially 
//                      with support for creating a surface object. So createLogicalDevice
//                      checks the physical devices available and creates a logical device
//                      (device) and a queue (queue).
// 
//******************************************************************************************

        void createLogicalDevice()
        {
            std::vector<vk::QueueFamilyProperties> queueFamilyProperties = physicalDevice.getQueueFamilyProperties();

            // Get the first index into queueFamilyProperties which supports both graphics and present
            for (uint32_t qfpIndex = 0; qfpIndex < queueFamilyProperties.size(); qfpIndex++)
            {
                if ((queueFamilyProperties[qfpIndex].queueFlags & vk::QueueFlagBits::eGraphics) &&
                     physicalDevice.getSurfaceSupportKHR(  qfpIndex
                                                         , *surface))
                {
                    // Found a queue family that supports both graphics and present
                    queueIndex = qfpIndex;
                    break;
                }
            }
            if (queueIndex == ~0)
            {
                throw std::runtime_error("Could not find a queue for graphics and present -> terminating...");
            }

            // Query for Vulkan 1.3 features
            vk::StructureChain<  vk::PhysicalDeviceFeatures2
                               , vk::PhysicalDeviceVulkan11Features
                               , vk::PhysicalDeviceVulkan13Features
                               , vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>
                featureChain = 
                {
                      {}                                        // vk::PhysicalDeviceFeatures2
                    , {.shaderDrawParameters = true}            // vk::PhysicalDeviceVulkan11Features
                    , {  .synchronization2 = true 
                       , .dynamicRendering = true}              // vk::PhysicalDeviceVulkan13Features
                    , {.extendedDynamicState = true}            // vk::PhysicalDeviceExtendedDynamicsStateFeaturesEXT
                };

            // Create a device
            float                       queuePriority       = 0.5f;
            vk::DeviceQueueCreateInfo   deviceQueueCreateInfo {  .queueFamilyIndex      = queueIndex
                                                               , .queueCount            = 1
                                                               , .pQueuePriorities      = &queuePriority};
            vk::DeviceCreateInfo        deviceCreateInfo {  .pNext                      = &featureChain.get<vk::PhysicalDeviceFeatures2>()
                                                          , .queueCreateInfoCount       = 1
                                                          , .pQueueCreateInfos          = &deviceQueueCreateInfo
                                                          , .enabledExtensionCount      = static_cast<uint32_t>(requiredDeviceExtension.size())
                                                          , .ppEnabledExtensionNames    = requiredDeviceExtension.data()};

            device          = vk::raii::Device(  physicalDevice
                                               , deviceCreateInfo);
            queue           = vk::raii::Queue(  device
                                              , queueIndex
                                              , 0);
        }
        

//******************************************************************************************
// 
//  Name:           createLogicalDevice
//  Arguments:      N/A
//  Returns:        void
//  Calls:          physicalDevice.getQueueFamilyProperties
//                  physicalDevice.getSurfaceSupportKHR
//                  vk::raii::Device
//                  vk::raii::Queue
//  Called by:      initVulkan
//  Description:    Since a logical device can be different from a physical device (a 
//                      logical device can be comprised of multiple physical devices,
//                      if I am not mistaken), the API can be used to interface with
//                      multiple physical devices, so all these system devices should be 
//                      checked to see if they are compatible with the API, especially 
//                      with support for creating a surface object. So createLogicalDevice
//                      checks the physical devices available and creates a logical device
//                      (device) and a queue (queue).
// 
//******************************************************************************************

        void createSwapChain()
        {
            vk::SurfaceCapabilitiesKHR surfaceCapabilities  = physicalDevice.getSurfaceCapabilitiesKHR(*surface);
            swapChainExtent                                 = chooseSwapExtent(surfaceCapabilities);
            uint32_t minImageCount                          = chooseSwapMinImageCount(surfaceCapabilities);

            std::vector<vk::SurfaceFormatKHR> availableFormats  = physicalDevice.getSurfaceFormatsKHR(*surface);
            swapChainSurfaceFormat                              = chooseSwapSurfaceFormat(availableFormats);

            std::vector<vk::PresentModeKHR> availablePresentModes   = physicalDevice.getSurfacePresentModesKHR(*surface);
            vk::PresentModeKHR              presentMode             = chooseSwapPresentMode(availablePresentModes);

            vk::SwapchainCreateInfoKHR swapChainCreateInfo {  .surface          = *surface
                                                            , .minImageCount    = minImageCount
                                                            , .imageFormat      = swapChainSurfaceFormat.format
                                                            , .imageColorSpace  = swapChainSurfaceFormat.colorSpace
                                                            , .imageExtent      = swapChainExtent
                                                            , .imageArrayLayers = 1
                                                            , .imageUsage       = vk::ImageUsageFlagBits::eColorAttachment
                                                            , .imageSharingMode = vk::SharingMode::eExclusive
                                                            , .preTransform     = surfaceCapabilities.currentTransform
                                                            , .compositeAlpha   = vk::CompositeAlphaFlagBitsKHR::eOpaque
                                                            , .presentMode      = presentMode
                                                            , .clipped          = true

            };

            swapChain       = vk::raii::SwapchainKHR(  device
                                                     , swapChainCreateInfo);
            swapChainImages = swapChain.getImages();
        }
        

//******************************************************************************************
// 
//  Name:           createImageViews
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void createImageViews()
        {
            assert(swapChainImageViews.empty());

            vk::ImageViewCreateInfo imageViewCreateInfo
            {
                  .viewType             = vk::ImageViewType::e2D
                , .format               = swapChainSurfaceFormat.format
                , .subresourceRange     = {
                      vk::ImageAspectFlagBits::eColor
                    , 0
                    , 1
                    , 0
                    , 1
                }
            };

            for (auto &image : swapChainImages)
            {
                imageViewCreateInfo.image = image;
                swapChainImageViews.emplace_back(  device
                                                 , imageViewCreateInfo);
            }
        }
        

//******************************************************************************************
// 
//  Name:           createGraphicsPipeline
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void createGraphicsPipeline()
        {
            vk::raii::ShaderModule shaderModule = createShaderModule(readFile("09_shaderModules/shaders/slang.spv"));

            vk::PipelineShaderStageCreateInfo               vertShaderStageInfo
            {
                  .stage                                    = vk::ShaderStageFlagBits::eVertex
                , .module                                   = shaderModule
                , .pName                                    = "vertMain"
            };

            vk::PipelineShaderStageCreateInfo               fragShaderStageInfo
            {
                  .stage                                    = vk::ShaderStageFlagBits::eFragment
                , .module                                   = shaderModule
                , .pName                                    = "fragMain"
            };

            vk::PipelineShaderStageCreateInfo               shaderStages[] = 
            {
                  vertShaderStageInfo
                , fragShaderStageInfo
            };

            vk::PipelineVertexInputStateCreateInfo          vertexInputInfo;

            vk::PipelineInputAssemblyStateCreateInfo        inputAssembly
            {
                  .topology                                 = vk::PrimitiveTopology::eTriangleList
            };

            vk::PipelineViewportStateCreateInfo             viewportState
            {
                  .viewportCount                            = 1
                , .scissorCount                             = 1
            };

            vk::PipelineRasterizationStateCreateInfo        rasterizer
            {
                  .depthClampEnable                         = vk::False
                , .rasterizerDiscardEnable                  = vk::False
                , .polygonMode                              = vk::PolygonMode::eFill
                , .cullMode                                 = vk::CullModeFlagBits::eBack
                , .frontFace                                = vk::FrontFace::eClockwise
                , .depthBiasEnable                          = vk::False
                , .lineWidth                                = 1.0f
            };

            vk::PipelineMultisampleStateCreateInfo          multisampling
            {
                  .rasterizationSamples                     = vk::SampleCountFlagBits::e1
                , .sampleShadingEnable                      = vk::False
            };

            vk::PipelineColorBlendAttachmentState           colorBlendAttachment
            {
                  .blendEnable                              = vk::False
                , .colorWriteMask                           =       vk::ColorComponentFlagBits::eR
                                                                |   vk::ColorComponentFlagBits::eG
                                                                |   vk::ColorComponentFlagBits::eB
                                                                |   vk::ColorComponentFlagBits::eA
            };

            vk::PipelineColorBlendStateCreateInfo           colorBlending
            {
                  .logicOpEnable                            = vk::False
                , .logicOp                                  = vk::LogicOp::eCopy
                , .attachmentCount                          = 1
                , .pAttachments                             = &colorBlendAttachment
            };

            std::vector<vk::DynamicState>                   dynamicStates = 
            {
                  vk::DynamicState::eViewport
                , vk::DynamicState::eScissor
            };
            
            vk::PipelineDynamicStateCreateInfo              dynamicState
            {
                  .dynamicStateCount                        = static_cast<uint32_t>(dynamicStates.size())
                , .pDynamicStates                           = dynamicStates.data()
            };

            vk::PipelineLayoutCreateInfo                    pipelineLayoutInfo
            {
                  .setLayoutCount                           = 0
                , .pushConstantRangeCount                   = 0
            };

            pipelineLayout                                  = vk::raii::PipelineLayout(  device
                                                                                   , pipelineLayoutInfo);

            vk::StructureChain<  vk::GraphicsPipelineCreateInfo
                               , vk::PipelineRenderingCreateInfo> pipelineCreateInfoChain
            {
                {
                      .stageCount                           = 2
                    , .pStages                              = shaderStages
                    , .pVertexInputState                    = &vertexInputInfo
                    , .pInputAssemblyState                  = &inputAssembly
                    , .pViewportState                       = &viewportState
                    , .pRasterizationState                  = &rasterizer
                    , .pMultisampleState                    = &multisampling
                    , .pColorBlendState                     = &colorBlending
                    , .pDynamicState                        = &dynamicState
                    , .layout                               = pipelineLayout
                    , .renderPass                           = nullptr  
                },
                {
                      .colorAttachmentCount                 = 1
                    , .pColorAttachmentFormats              = &swapChainSurfaceFormat.format
                }
            };

            graphicsPipeline = vk::raii::Pipeline(  device
                                                  , nullptr
                                                  , pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
        }
        

//******************************************************************************************
// 
//  Name:           createCommandPool
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void createCommandPool()
        {
            vk::CommandPoolCreateInfo poolInfo
            {
                  .flags                                    = vk::CommandPoolCreateFlagBits::eResetCommandBuffer
                , .queueFamilyIndex                         = queueIndex
            };
            commandPool = vk::raii::CommandPool(  device
                                                , poolInfo);
        }
        

//******************************************************************************************
// 
//  Name:           createCommandBuffers
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void createCommandBuffers()
        {
            commandBuffers.clear();
            vk::CommandBufferAllocateInfo allocInfo
            {
                  .commandPool                              = commandPool
                , .level                                    = vk::CommandBufferLevel::ePrimary
                , .commandBufferCount                       = MAX_FRAMES_IN_FLIGHT
            };
            commandBuffers                                  = vk::raii::CommandBuffers(  device
                                                                                       , allocInfo);
        }
        

//******************************************************************************************
// 
//  Name:           recordCommandBuffer
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void recordCommandBuffer(uint32_t imageIndex)
        {
            auto &commandBuffer                             = commandBuffers[frameIndex];
            commandBuffer.begin({});

            // Before stargin rendering, transition the swapchain image to vk::ImageLayout::eColorAttachmentOptimal
            transition_image_layout(
                  imageIndex
                , vk::ImageLayout::eUndefined
                , vk::ImageLayout::eColorAttachmentOptimal
                , {}                                                    // scrAccessMask (No need to wait for previous operations)
                , vk::AccessFlagBits2::eColorAttachmentWrite            // dstAccessMask
                , vk::PipelineStageFlagBits2::eColorAttachmentOutput    // srcStage
                , vk::PipelineStageFlagBits2::eColorAttachmentOutput    // dstStage
            );

            vk::ClearValue                  clearColor      = vk::ClearColorValue(  0.0f
                                                                                  , 0.0f
                                                                                  , 0.0f
                                                                                  , 1.0f);

            vk::RenderingAttachmentInfo     attachmentInfo  =
            {
                  .imageView                                = swapChainImageViews[imageIndex]
                , .imageLayout                              = vk::ImageLayout::eColorAttachmentOptimal
                , .loadOp                                   = vk::AttachmentLoadOp::eClear
                , .storeOp                                  = vk::AttachmentStoreOp::eStore
                , .clearValue                               = clearColor
            };

            vk::RenderingInfo               renderingInfo   = 
            {
                  .renderArea                               = 
                  {
                      .offset                               = {0, 0}
                    , .extent                               = swapChainExtent
                  }
                , .layerCount                               = 1
                , .colorAttachmentCount                     = 1
                , .pColorAttachments                        = &attachmentInfo
            };

            commandBuffer.beginRendering(renderingInfo);
            commandBuffer.bindPipeline(  vk::PipelineBindPoint::eGraphics
                                       , *graphicsPipeline);
            commandBuffer.setViewport(  0
                                      , vk::Viewport(  0.0f
                                                     , 0.0f
                                                     , static_cast<float>(swapChainExtent.width)
                                                     , static_cast<float>(swapChainExtent.height)
                                                     , 0.0f
                                                     , 1.0f)
                                     );
            commandBuffer.setScissor(  0
                                     , vk::Rect2D(vk::Offset2D(0, 0)
                                     , swapChainExtent));
            commandBuffer.draw(  3
                               , 1
                               , 0
                               , 0);
            commandBuffer.endRendering();
            commandBuffer.endRendering();

            // After rendering, transition the swapchain image to vk::ImageLayout::ePresentSrcKHR
            transition_image_layout(
                  imageIndex
                , vk::ImageLayout::eColorAttachmentOptimal
                , vk::ImageLayout::ePresentSrcKHR
                , vk::AccessFlagBits2::eColorAttachmentWrite            // srcAccessMask
                , {}                                                    // dstAccessMask
                , vk::PipelineStageFlagBits2::eColorAttachmentOutput    // srcStage
                , vk::PipelineStageFlagBits2::eBottomOfPipe             // dstStage
            );
            commandBuffer.end();
        }


//******************************************************************************************
// 
//  Name:           transition_image_layout
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void transition_image_layout(
              uint32_t                                      imageIndex
            , vk::ImageLayout                               old_layout
            , vk::ImageLayout                               new_layout
            , vk::AccessFlags2                              src_access_mask
            , vk::AccessFlags2                              dst_access_mask
            , vk::PipelineStageFlags2                       src_stage_mask
            , vk::PipelineStageFlags2                       dst_stage_mask
        )
        {
            vk::ImageMemoryBarrier2                         barrier = 
            {
                  .srcStageMask                             = src_stage_mask
                , .srcAccessMask                            = src_access_mask
                , .dstStageMask                             = dst_stage_mask
                , .dstAccessMask                            = dst_access_mask
                , .oldLayout                                = old_layout
                , .srcQueueFamilyIndex                      = VK_QUEUE_FAMILY_IGNORED
                , .dstQueueFamilyIndex                      = VK_QUEUE_FAMILY_IGNORED
                , .image                                    = swapChainImages[imageIndex]
                , .subresourceRange                         = 
                {
                      .aspectMask                           = vk::ImageAspectFlagBits::eColor
                    , .baseMipLevel                         = 0
                    , .levelCount                           = 1
                    , .baseArrayLayer                       = 0
                    , .layerCount                           = 1
                }
            };

            vk::DependencyInfo dependency_info              =
            {
                  .dependencyFlags                          = {}
                , .imageMemoryBarrierCount                  = 1
                , .pImageMemoryBarriers                     = &barrier
            };

            commandBuffers[frameIndex].pipelineBarrier2(dependency_info);
        }


//******************************************************************************************
// 
//  Name:           createSyncObjects
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void createSyncObjects()
        {
            assert(   presentCompleteSemaphores.empty() 
                   && renderFinishedSemaphores.empty()
                   && inFlightFences.empty());

            for (size_t i = 0; i < swapChainImages.size(); i++)
            {
                renderFinishedSemaphores.emplace_back(  device
                                                      , vk::SemaphoreCreateInfo());
            }

            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            {
                presentCompleteSemaphores.emplace_back(  device
                                                       , vk::SemaphoreCreateInfo());
                inFlightFences.emplace_back(  device
                                            , vk::FenceCreateInfo
                                            {  
                                                .flags = vk::FenceCreateFlagBits::eSignaled
                                            });
            }
        }


//******************************************************************************************
// 
//  Name:           drawFrame
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void drawFrame()
        {
            // Note: inFlightFences, presentCompleteSemaphores, and commandBuffers are indexed by frameIndex,
            //       while renderFinishedSemaphores is index by imageIndex
            auto fenceResult                                =  device.waitForFences(  *inFlightFences[frameIndex]
                                                                                    , vk::True
                                                                                    , UINT64_MAX);
            if (fenceResult != vk::Result::eSuccess)
            {
                throw std::runtime_error("Failed to wait for fence!");
            }
            
            auto [  result
                , imageIndex] = swapChain.acquireNextImage(  UINT64_MAX
                    , *presentCompleteSemaphores[frameIndex]
                    , nullptr);

            // Due to VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS being defined, eErrorOutOfDateKHR can be checked as a result
            // here and does not need to be caugfht by and exception.

            if (result == vk::Result::eErrorOutOfDateKHR)
            {
                recreateSwapChain();
                return;
            }

            // On other success codes than eSuccess and eSuboptimalKHR we just throw an exception.
            // On any error code, aquireNextImage already threw and exception.

            if (   result != vk::Result::eSuccess 
                && result != vk::Result::eSuboptimalKHR)
            {
                assert(  result == vk::Result::eTimeout 
                      || result == vk::Result::eNotReady);
                throw std::runtime_error("Failed to acquire swap chain image!");
            }

            // Only reset the fence if we are submitting work                    
            device.resetFences(*inFlightFences[frameIndex]);

            commandBuffers[frameIndex].reset();
            recordCommandBuffer(imageIndex);

            vk::PipelineStageFlags  waitDestinationStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);

            const vk::SubmitInfo        submitInfo
            {
                  .waitSemaphoreCount                       = 1
                , .pWaitSemaphores                          = &*presentCompleteSemaphores[frameIndex]
                , .pWaitDstStageMask                        = &waitDestinationStageMask
                , .commandBufferCount                       = 1
                , .pCommandBuffers                          = &*commandBuffers[frameIndex]
                , .signalSemaphoreCount                     = 1
                , .pSignalSemaphores                        = &*renderFinishedSemaphores[imageIndex]
            };

            queue.submit(  submitInfo
                         , *inFlightFences[frameIndex]);

            const vk::PresentInfoKHR    presentInfoKHR
            {
                  .waitSemaphoreCount                       = 1
                , .pWaitSemaphores                          = &*renderFinishedSemaphores[imageIndex]
                , .swapchainCount                           = 1
                , .pSwapchains                              = &*swapChain
                , .pImageIndices                            = &imageIndex
            };

            result = queue.presentKHR(presentInfoKHR);

            // Due to VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS being defined, eErrorOutOfDateKHR can be checked as a result
            // here and does not need to be caught by an exception.

            if (   (result == vk::Result::eSuboptimalKHR) 
                || (result == vk::Result::eErrorOutOfDateKHR) 
                || framebufferResized)
            {
                framebufferResized = false;
                recreateSwapChain();
            }
            else
            {
                // There are no other success codes than eSuccess; on any error code, presentKHR already threw an exception.
                assert(result == vk::Result::eSuccess);
            }

            frameIndex = (frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
        }


//******************************************************************************************
// 
//  Name:           createShaderModule
//  Arguments:      N/A
//  Returns:        [[nodiscard]] vk::raii::ShaderModule
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

    [[nodiscard]] vk::raii::ShaderModule createShaderModule(const std::vector<char> &code) const
    {
        vk::ShaderModuleCreateInfo createInfo
        {
              .codeSize     = code.size() * sizeof(char)
            , .pCode        = reinterpret_cast<const uint32_t *>(code.data())
        };

        vk::raii::ShaderModule shaderModule
        {
              device
            , createInfo
        };
        
        return shaderModule;
    }
        

//******************************************************************************************
// 
//  Name:           chooseSwapMinImageCount
//  Arguments:      N/A
//  Returns:        
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        static uint32_t chooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const &surfaceCapabilities)
        {
            auto minImageCount = std::max(  3u
                                          , surfaceCapabilities.minImageCount);
            if ((0 < surfaceCapabilities.maxImageCount) && (surfaceCapabilities.maxImageCount < minImageCount))
            {
                minImageCount = surfaceCapabilities.maxImageCount;
            }
            return minImageCount;
        }
        

//******************************************************************************************
// 
//  Name:           chooseSwapSurfaceFormat
//  Arguments:      N/A
//  Returns:        static vk::SurfaceFormatKHR
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        static vk::SurfaceFormatKHR chooseSwapSurfaceFormat(std::vector<vk::SurfaceFormatKHR> const &availableFormats)
        {
            assert(!availableFormats.empty());
            const auto formatIt = std::ranges::find_if(  availableFormats
                                                       , [](const auto &format) 
                                                       {
                                                            return format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
                                                       });
            return formatIt != availableFormats.end() ? *formatIt : availableFormats[0];
        }

        static vk::PresentModeKHR chooseSwapPresentMode(std::vector<vk::PresentModeKHR> const &availablePresentModes)
        {
            assert(std::ranges::any_of(  availablePresentModes
                                       , [](auto presentMode)
                                    {
                                        return presentMode == vk::PresentModeKHR::eFifo;
                                    }));
            return std::ranges::any_of(  availablePresentModes
                                       , [](const vk::PresentModeKHR value)
                                    {
                                        return vk::PresentModeKHR::eMailbox == value;
                                    }) ?
                                    vk::PresentModeKHR::eMailbox :
                                    vk::PresentModeKHR::eFifo;
        }
        

//******************************************************************************************
// 
//  Name:           chooseSwapExtent
//  Arguments:      N/A
//  Returns:        vk::Extent2D
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        vk::Extent2D chooseSwapExtent(vk::SurfaceCapabilitiesKHR const &capabilities)
        {
            if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
            {
                return capabilities.currentExtent;
            }
            int width, height;
            glfwGetFramebufferSize(  window
                                   , &width
                                   , &height);

            return {
                  std::clamp<uint32_t>(  width
                                       , capabilities.minImageExtent.width
                                       , capabilities.maxImageExtent.width)
                , std::clamp<uint32_t>(  height
                                       , capabilities.minImageExtent.height
                                       , capabilities.minImageExtent.height)
            };
        }


//******************************************************************************************
// 
//  Name:           getRequiredInstanceExtensions
//  Arguments:      N/A
//  Returns:        std::vector<const char *>
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
//  Returns:        static VKAPI_ATTR vk::Bool32 VKAPI_CALL
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
        

//******************************************************************************************
// 
//  Name:           readFile
//  Arguments:      filename
//  Returns:        static std::vector<char>
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        static std::vector<char> readFile(const std::string &filename)
        {
            std::ifstream file(  filename
                               , std::ios::ate | std::ios::binary);
            if (!file.is_open())
            {
                throw std::runtime_error("Failed to open file!");
            }
            std::vector<char> buffer(file.tellg());
            file.seekg(  0
                       , std::ios::beg
                    );
            file.read(  buffer.data()
                      , static_cast<std::streamsize>(buffer.size())
                    );
            file.close();
            return buffer;

        }
};
        

//******************************************************************************************
// 
//  Name:           main
//  Arguments:      N/A
//  Returns:        int
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