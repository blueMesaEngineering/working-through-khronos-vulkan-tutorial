#include <algorithm>
#include <array>
#include <assert.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#   include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif
//#include <vulkan/vulkan_profiles.hpp>
#include "/home/nik/Vulkan/install-version-1.4.350.0/1.4.350.0/x86_64/include/vulkan/vulkan_profiles.hpp"

#define GLFW_INCLUDE_VULKAN        // REQUIRED only for GLFW CreateWindowSurface.
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

constexpr uint32_t                  WIDTH                   = 800;
constexpr uint32_t                  HEIGHT                  = 600;
const std::string                   MODEL_PATH              = "models/viking_room.obj";
const std::string                   TEXTURE_PATH            = "textures/viking_room.png";
constexpr int                       MAX_FRAMES_IN_FLIGHT    = 2;

// Application info structure to store profile support flags

struct AppInfo
{
	bool			profileSupported		= false;
	VpProfileProperties	profile;
};

// Moved struct definitions inside the class
struct Vertex
{
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord;

    static vk::VertexInputBindingDescription getBindingDescription()
    {
        return 
	{
              0
            , sizeof(Vertex)
            , vk::VertexInputRate::eVertex
        };
    }

    static std::array<vk::VertexInputAttributeDescription, 3> getAttributeDescriptions()
    {
        return
        {
            vk::VertexInputAttributeDescription
            (    
                   0
                ,  0
                , vk::Format::eR32G32B32Sfloat
                , offsetof(  Vertex
                           , pos)
            )
            , vk::VertexInputAttributeDescription
            (
                  1
                , 0
                , vk::Format::eR32G32B32Sfloat
                , offsetof(  Vertex
                           , color)
            )
            , vk::VertexInputAttributeDescription
            (
                  2
                , 0
                , vk::Format::eR32G32Sfloat
                , offsetof(  Vertex
                           , texCoord)
            )
        };
    }

    bool operator==(const Vertex &other) const
    {
        return    pos       == other.pos 
               && color     == other.color 
               && texCoord  == other.texCoord;
    }
};

template <>
struct std::hash<Vertex>
{
    size_t operator()(Vertex const &vertex) const noexcept
    {
        return 
        (
            (
                   hash<glm::vec3>()(vertex.pos) 
                ^ (hash<glm::vec3>()(vertex.color) << 1)
            ) >> 1
        ) 
        ^ 
        (
            hash<glm::vec2>()(vertex.texCoord) << 1
        );
    }
};

struct UniformBufferObject
{
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
};

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

        // Initial set up and swapchain
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

        // Traditional render pass (fallback for non-dynamic rendering)
        vk::raii::RenderPass                    renderPass                  = nullptr;

        // Descriptor sets and pipeline
        vk::raii::DescriptorSetLayout           descriptorSetLayout         = nullptr;
        vk::raii::PipelineLayout                pipelineLayout              = nullptr;
        vk::raii::Pipeline                      graphicsPipeline            = nullptr;
        std::vector<vk::raii::Framebuffer>      swapChainFramebuffers;

        // Command pool
        vk::raii::CommandPool                   commandPool                 = nullptr;
        std::vector<vk::raii::CommandBuffer>    commandBuffers;

        // Synchronization objects - Semaphores and fences
	    std::vector<vk::raii::Semaphore>	    imageAvailableSemaphores;
        std::vector<vk::raii::Semaphore>        renderFinishedSemaphores;
        std::vector<vk::raii::Fence>            inFlightFences;
        std::vector<vk::raii::Semaphore>        presentCompleteSemaphore;
        uint32_t                                frameIndex                  = 0;

        bool framebufferResized                                             = false;

        vk::raii::Buffer                        vertexBuffer                = nullptr;
        vk::raii::DeviceMemory                  vertexBufferMemory          = nullptr;
        vk::raii::Buffer                        indexBuffer                 = nullptr;
        vk::raii::DeviceMemory                  indexBufferMemory           = nullptr;

        // Uniform buffers
        std::vector<vk::raii::Buffer>           uniformBuffers;
        std::vector<vk::raii::DeviceMemory>     uniformBuffersMemory;
        std::vector<void *>                     uniformBuffersMapped;

        // Descriptor pool
        vk::raii::DescriptorPool                descriptorPool              = nullptr;
        std::vector<vk::raii::DescriptorSet>    descriptorSets;

        // Mipmapping
        vk::raii::Image                         textureImage                = nullptr;
        vk::raii::DeviceMemory                  textureImageMemory          = nullptr;
        vk::raii::ImageView                     textureImageView            = nullptr;
        vk::raii::Sampler                       textureSampler              = nullptr;
	
        // Depth management
        vk::raii::Image                         depthImage                  = nullptr;
        vk::raii::DeviceMemory                  depthImageMemory            = nullptr;
        vk::raii::ImageView                     depthImageView              = nullptr;
	
        // Vertices
        std::vector<Vertex>                     vertices;
        std::vector<uint32_t>                   indices;
        vk::SampleCountFlagBits                 msaaSamples                 = vk::SampleCountFlagBits::e1;
	
        // Color management
        vk::raii::Image                         colorImage                  = nullptr;
        vk::raii::DeviceMemory                  colorImageMemory            = nullptr;
        vk::raii::ImageView                     colorImageView              = nullptr;

        // Application info to store profile support
        AppInfo appInfo                                                     = {};

        struct SwapChainSupportDetails
        {
            vk::SurfaceCapabilitiesKHR          capabilities;
            std::vector<vk::SurfaceFormatKHR>   formats;
            std::vector<vk::PresentModeKHR>     presentModes;
        };

        const std::vector<const char *>         requiredDeviceExtension     =
        {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
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

        static void framebufferResizeCallback(
              GLFWwindow *window
            , int
            , int
        )
        {
            auto app                                        = reinterpret_cast<HelloTriangleApplication *>(glfwGetWindowUserPointer(window));
            app->framebufferResized                         = true;
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
//                  getMaxUsableSampleCount
//                  createLogicalDevice
//                  createSwapChain
//                  createImageViews
//                  createDescriptorSetLayout
//                  createGraphicsPipeline
//                  createCommandPool
//                  createDepthResources
//                  createTextureImage
//                  createTextureImageView
//                  createTextureSampler
//                  loadModel
//                  createVertexBuffer
//                  createIndexBuffer
//                  createUniformBuffers
//                  createDescriptorPool
//                  createDescriptorSets
//                  createCommandBuffers
//                  createSyncObjects
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
	        checkFeatureSupport();
            createLogicalDevice();
            createSwapChain();
            createImageViews();
	    
            // Create render pass only if not using dynamic rendering
            if (!appInfo.profileSupported)
            {
                createRenderPass();
            }
	    
            createDescriptorSetLayout();
            createGraphicsPipeline();
	    
            // Create framebuffers only if not using dynamic rendering
            if (!appInfo.profileSupported)
            {
                createFramebuffers();
            }
	    
            createCommandPool();
            createColorResources();
            createDepthResources();
            createTextureImage();
            createTextureImageView();
            createTextureSampler();
            loadModel();
            createVertexBuffer();
            createIndexBuffer();
            createUniformBuffers();
            createDescriptorPool();
            createDescriptorSets();
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
            swapChainFramebuffers.clear();
            swapChainImageViews.clear();
            
            // Semaphores tied to swapchain image indices need to be rebuilt on resize
            presentCompleteSemaphore.clear();
            
            for (auto &imageView : swapChainImageViews)
            {
                imageView			                        = nullptr;
            }
            
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
	    
            // Recreate traditional render pass and framebuffers if not using profiles
            if (!appInfo.profileSupported)
            {
                createRenderPass();
                createFramebuffers();
            }
            
            createColorResources();
            createDepthResources();
            
            // Recreate per-swapchain-image present semaphores after resize
            presentCompleteSemaphore.reserve(swapChainImages.size());
            vk::SemaphoreCreateInfo semaphoreInfo{};

            for (size_t i = 0; i < swapChainImages.size(); ++i)
            {
                presentCompleteSemaphore.push_back(device.createSemaphore(semaphoreInfo));
            }
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
                  .pApplicationName     = "Vulkan Profiles Demo"
                , .applicationVersion   = VK_MAKE_VERSION(1, 0, 0)
                , .pEngineName          = "No Engine"
                , .engineVersion        = VK_MAKE_VERSION(1, 0, 0)
                , .apiVersion           = vk::ApiVersion14
            };
	    
            // Get the required extensions.
            auto extensions = getRequiredInstanceExtensions();
            
            vk::InstanceCreateInfo createInfo
            {
                  .pApplicationInfo         = &appInfo
                , .enabledExtensionCount    = static_cast<uint32_t>(extensions.size())
                , .ppEnabledExtensionNames  = extensions.data()
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
            // Always set up the debug messenger
            // It will only be used if validation layers are enabled via vulkanconfig

            vk::DebugUtilsMessageSeverityFlagsEXT   severityFlags(
                  vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose
                | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning 
                | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError
            );

            vk::DebugUtilsMessageTypeFlagsEXT       messageTypeFlags(  
		          vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral 
                | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance 
                | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation
	       );

            vk::DebugUtilsMessengerCreateInfoEXT    debugUtilsMessengerCreateInfoEXT
            {
                  .messageSeverity                          = severityFlags
                , .messageType                              = messageTypeFlags
                , .pfnUserCallback                          = &debugCallback
            };
	    
            try
            {
                debugMessenger = instance.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfoEXT);
            }
            catch (vk::SystemError &err)
            {
                // If the debug utils extension is not available, this will fail
                // That's Ok, it just means validation layers aren't enabled

                std::cout << "Debug messenger not available. Validation layers may not be enabled." << std::endl;
            }
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
            if (glfwCreateWindowSurface(  
                  *instance
                , window
                , nullptr
                , &_surface) != 0
               )
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
                                                                    }
                                                                  );
                                    });

            // Check if the physicalDevice supports the required features
            // Return true if the physicalDevice meets all the criteria
            return supportsVulkan1_3 && supportsGraphics && supportsAllRequiredExtensions;
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
            physicalDevice                                          = *devIter;
	        msaaSamples			                                    = getMaxUsableSampleCount();
	    
            // Printe device information
            vk::PhysicalDeviceProperties 	deviceProperties	    = physicalDevice.getProperties();

            std::cout << "Selected GPU: " << deviceProperties.deviceName                    << std::endl;
            std::cout << "API Version: "  << VK_VERSION_MAJOR(deviceProperties.apiVersion)  << "."
                      << VK_VERSION_MINOR(deviceProperties.apiVersion)                      << "."
                      << VK_VERSION_PATCH(deviceProperties.apiVersion)                      << std::endl;
        }

        

//******************************************************************************************
// 
//  Name:           checkFeatureSupport()
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

		void checkFeatureSupport()
		{
			// Define the KHR roadmap 2022 profile - more widely supported than 2024
			appInfo.profile = 
			{
				  VP_KHR_ROADMAP_2022_NAME
				, VP_KHR_ROADMAP_2022_SPEC_VERSION
			};
			
			// Check if the profile is supported
			VkBool32	            supported	            = VK_FALSE;
			VkResult	            result		            = vpGetPhysicalDeviceProfileSupport(
                                                                                              *instance
                                                                                            , *physicalDevice
                                                                                            , &appInfo.profile
                                                                                            , &supported
                                                                                            );
			
			if (result == VK_SUCCESS && supported == VK_TRUE)
			{
				appInfo.profileSupported	                = true;
				std::cout << "Using KHR roadmap 2022 profile" << std::end;
			}
			else
			{
				appInfo.profileSupported 	                = false;
				std::cout << "Falling back to traditional rendering (profile not supported)" << std::end;
				
				// If we wanted to implement fallback, we would call detectFeatureSupport() here
				// But for this example, we'll just use traditional rendering if the profile isn't supported
			}
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
                    queueIndex                              = qfpIndex;
                    break;
                }
            }
            if (queueIndex == ~0)
            {
                throw std::runtime_error("Could not find a queue for graphics and present -> terminating...");
            }

            float                       queuePriority       = 0.5f;
            vk::DeviceQueueCreateInfo   deviceQueueCreateInfo 
            {
                  .queueFamilyIndex                         = queueIndex
                , .queueCount                               = 1
                , .pQueuePriorities                         = &queuePriority
            };

            if (appInfo.profileSupported)
            {
                // Create device with Best Practices profile
                
                // Enable required features
                vk::PhysicalDeviceFeatures2	                features2;
                vk::PhysicalDeviceFeatures	                deviceFeatures{};
                deviceFeatures.samplerAnisotropy		    = VK_TRUE;
                deviceFeatures.sampleRateShading		    = VK_TRUE;
                features2.features				            = deviceFeatures;
                
                // Enable dynamic rendering
                vk::PhysicalDeviceDynamicRenderingFeatures	dynamicRenderingFeatures;
                dynamicRenderingFeatures.dynamicRendering	= VK_TRUE;
                features2.pNext					            = &dynamicRenderingFeatures;
                
                // Create a vk::DeviceCreateInfo with the required features
                vk::DeviceCreateInfo                        vkDeviceCreateInfo 
                {
                      .pNext                                = &features2
                    , .queueCreateInfoCount                 = 1
                    , .pQueueCreateInfos                    = &deviceQueueCreateInfo
                    , .enabledExtensionCount                = static_cast<uint32_t>(requiredDeviceExtension.size())
                    , .ppEnabledExtensionNames              = requiredDeviceExtension.data()
                };
                
                // Create the device with the vk::DeviceCreateInfo
                device 			                            = vk::raii::Device(  physicalDevice
                                                                               , vkDeviceCreateInfo);
                                        
                std::cout << "Created logical device using KHR roadmap 2022 profile" << std::endl;
            }
            else
            {
                // Fallback to manual device creation
                vk::PhysicalDeviceFeatures	                deviceFeatures{};
                deviceFeatures.samplerAnisotropy	        = VK_TRUE;
                deviceFeatures.sampleRateShading	        = VK_TRUE;
                
                vk::DeviceCreateInfo		                createInfo 
                {
                      .queueCreateInfoCount		            = 1
                    , .pQueueCreateInfos		            = &deviceQueueCreateInfo
                    , .enabledExtensionCount	            = static_cast<uint32_t>(requiredDeviceExtension.size())
                    , .ppEnabledExtensionNames	            = requiredDeviceExtension.data()
                    , .pEnabledFeatures		                = &deviceFeatures
                };

                device                                      = vk::raii::Device(  physicalDevice
                                                                               , createInfo);
                                
                std::cout << "Created logical device using manual feature selection" << std::endl;
            }
	    
            queue                                           = device.getQueue(  queueIndex
                                                                              , 0);
        }
        

//******************************************************************************************
// 
//  Name:           createSwapChain
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void createSwapChain()
        {
            vk::SurfaceCapabilitiesKHR surfaceCapabilities          = physicalDevice.getSurfaceCapabilitiesKHR(*surface);
            swapChainExtent                                         = chooseSwapExtent(surfaceCapabilities);
            uint32_t minImageCount                                  = chooseSwapMinImageCount(surfaceCapabilities);

            std::vector<vk::SurfaceFormatKHR> availableFormats      = physicalDevice.getSurfaceFormatsKHR(*surface);
            swapChainSurfaceFormat                                  = chooseSwapSurfaceFormat(availableFormats);

            std::vector<vk::PresentModeKHR> availablePresentModes   = physicalDevice.getSurfacePresentModesKHR(*surface);
            vk::PresentModeKHR              presentMode             = chooseSwapPresentMode(availablePresentModes);

            vk::SwapchainCreateInfoKHR      swapChainCreateInfo 
            {  
                  .surface                                          = *surface
                , .minImageCount                                    = minImageCount
                , .imageFormat                                      = swapChainSurfaceFormat.format
                , .imageColorSpace                                  = swapChainSurfaceFormat.colorSpace
                , .imageExtent                                      = swapChainExtent
                , .imageArrayLayers                                 = 1
                , .imageUsage                                       = vk::ImageUsageFlagBits::eColorAttachment
                , .imageSharingMode                                 = vk::SharingMode::eExclusive
                , .preTransform                                     = surfaceCapabilities.currentTransform
                , .compositeAlpha                                   = vk::CompositeAlphaFlagBitsKHR::eOpaque
                , .presentMode                                      = presentMode
                , .clipped                                          = true

            };

            swapChain                                               = device.createSwapchainKHR(swapChainCreateInfo);

            swapChainImages                                         = swapChain.getImages();
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
	    swapChainImageViews.reserve(swapChainImages.size());
	    
	    for (const auto &image : swapChainImages)
            {
                swapChainImageViews.push_back(createImageView(  image
                                                              , swapChainSurfaceFormat.format
                                                              , vk::ImageAspectFlagBits::eColor
                                                              , 1)
                                                            );
            }
        }
        

//******************************************************************************************
// 
//  Name:           createRenderPass
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void createRenderPass()
        {
		// This is only called if the Best Practices profile is not supported
		// or if dynamic rendering is not available
            vk::AttachmentDescription   colorAttachment
            {
                  .format                                   = swapChainSurfaceFormat.format
                , .samples                                  = msaaSamples
                , .loadOp                                   = vk::AttachmentLoadOp::eClear
                , .storeOp                                  = vk::AttachmentStoreOp::eStore
                , .stencilLoadOp                            = vk::AttachmentLoadOp::eDontCare
                , .stencilStoreOp                           = vk::AttachmentStoreOp::eDontCare
                , .initialLayout                            = vk::ImageLayout::eUndefined
                , .finalLayout                              = vk::ImageLayout::eColorAttachmentOptimal
            };

            vk::AttachmentDescription   depthAttachment
            {
                  .format                                   = findDepthFormat()
                , .samples                                  = msaaSamples
                , .loadOp                                   = vk::AttachmentLoadOp::eClear
                , .storeOp                                  = vk::AttachmentStoreOp::eDontCare
                , .stencilLoadOp                            = vk::AttachmentLoadOp::eDontCare
                , .stencilStoreOp                           = vk::AttachmentStoreOp::eDontCare
                , .initialLayout                            = vk::ImageLayout::eUndefined
                , .finalLayout                              = vk::ImageLayout::eDepthStencilAttachmentOptimal
            };

            vk::AttachmentDescription   colorAttachmentResolve
            {
                  .format                                   = swapChainSurfaceFormat.format
                , .samples                                  = vk::SampleCountFlagBits::e1
                , .loadOp                                   = vk::AttachmentLoadOp::eDontCare
                , .storeOp                                  = vk::AttachmentStoreOp::eStore
                , .stencilLoadOp                            = vk::AttachmentLoadOp::eDontCare
                , .stencilStoreOp                           = vk::AttachmentStoreOp::eDontCare
                , .initialLayout                            = vk::ImageLayout::eUndefined
                , .finalLayout                              = vk::ImageLayout::ePresentSrcKHR
            };

            // Subpass references
            vk::AttachmentReference     colorAttachmentRef
            {
                  .attachment                               = 0
                , .layout                                   = vk::ImageLayout::eColorAttachmentOptimal
            };

            vk::AttachmentReference     depthAttachmentRef
            {
                  .attachment                               = 1
                , .layout                                   = vk::ImageLayout::eDepthStencilAttachmentOptimal
            };

            vk::AttachmentReference     colorAttachmentResolveRef
            {
                  .attachment                               = 2
                , .layout                                   = vk::ImageLayout::eColorAttachmentOptimal
            };

            // Subpass description
            vk::SubpassDescription      subpass
            {
                  .pipelineBindPoint                        = vk::PipelineBindPoint::eGraphics
                , .colorAttachmentCount                     = 1
                , .pColorAttachments                        = &colorAttachmentRef
                , .pResolveAttachments                      = &colorAttachmentResolveRef
                , .pDepthStencilAttachment                  = &depthAttachmentRef
            };

            // Dependency to ensure proper image layout transitions
            vk::SubpassDependency       dependency
            {
                  .srcSubpass                               = VK_SUBPASS_EXTERNAL
                , .dstSubpass                               = 0
                , .srcStageMask                             =   vk::PipelineStageFlagBits::eColorAttachmentOutput
                                                              | vk::PipelineStageFlagBits::eEarlyFragmentTests
                , .dstStageMask                             =   vk::PipelineStageFlagBits::eColorAttachmentOutput
                                                              | vk::PipelineStageFlagBits::eEarlyFragmentTests
                , .srcAccessMask                            =   vk::AccessFlagBits::eNone
                , .dstAccessMask                            =   vk::AccessFlagBits::eColorAttachmentWrite
                                                              | vk::AccessFlagBits::eDepthStencilAttachmentWrite
            };

            // Create the render pass
            std::array<vk::AttachmentDescription, 3>        attachments =
            {
                  colorAttachment
                , depthAttachment
                , colorAttachmentResolve
            };

            vk::RenderPassCreateInfo    renderPassInfo
            {
                  .attachmentCount                          = static_cast<uint32_t>(attachments.size())
                , .pAttachments                             = attachments.data()
                , .subpassCount                             = 1
                , .pSubpasses                               = &subpass
                , .dependencyCount                          = 1
                , .pDependencies                            = &dependency
            };

            renderPass                                      = device.createRenderPass(renderPassInfo);
        }
        

//******************************************************************************************
// 
//  Name:           createDescriptorSetLayout
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void createDescriptorSetLayout()
        {
            vk::DescriptorSetLayoutBinding      uboLayoutBinding
            {
                  .binding			                        = 0
                , .descriptorType		                    = vk::DescriptorType::eUniformBuffer
                , .descriptorCount		                    = 1
                , .stageFlags			                    = vk::ShaderStageFlagBits::eVertex
		    };
		
		    vk::DescriptorSetLayoutBinding	    samplerLayoutBinding
		    {
                  .binding			                        = 1
                , .descriptorType		                    = vk::DescriptorType::eCombinedImageSampler
                , .descriptorCount		                    = 1
                , .stageFlags			                    = vk::ShaderStageFlagBits::eFragment
            };
		    
		    std::array<vk::DescriptorSetLayoutBinding, 2> bindings =
		    {
                  uboLayoutBinding
                , samplerLayoutBinding
		    };

            vk::DescriptorSetLayoutCreateInfo   layoutInfo
            {
                  .bindingCount                             = static_cast<uint32_t>(bindings.size())
                , .pBindings                                = bindings.data()
            };

            descriptorSetLayout                             = device.createDescriptorSetLayout(layoutInfo);
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
            auto                    vertShaderCode          = readFile("shaders/vert.spv");
            auto                    fragShaderCode          = readFile("shaders/frag.spv");

		    vk::raii::ShaderModule  vertShaderModule		= createShaderModule(vertShaderCode);
		    vk::raii::ShaderModule  fragShaderModule		= createShaderModule(fragShaderCode);
		
            vk::PipelineShaderStageCreateInfo               vertShaderStageInfo
            {
                  .stage                                    = vk::ShaderStageFlagBits::eVertex
                , .module                                   = vertShaderModule
                , .pName                                    = "main"
            };

            vk::PipelineShaderStageCreateInfo               fragShaderStageInfo
            {
                  .stage                                    = vk::ShaderStageFlagBits::eFragment
                , .module                                   = *fragShaderModule
                , .pName                                    = "main"
            };

            vk::PipelineShaderStageCreateInfo               shaderStages[] = 
            {
                  vertShaderStageInfo
                , fragShaderStageInfo
            };

            auto                    bindingDescription      = Vertex::getBindingDescription();
            auto                    attributeDescriptions   = Vertex::getAttributeDescriptions();

            vk::PipelineVertexInputStateCreateInfo          vertexInputInfo
            {
                  .vertexBindingDescriptionCount            = 1
                , .pVertexBindingDescriptions               = &bindingDescription
                , .vertexAttributeDescriptionCount          = static_cast<uint32_t>(attributeDescriptions.size())
                , .pVertexAttributeDescriptions             = attributeDescriptions.data()
            };

            vk::PipelineInputAssemblyStateCreateInfo        inputAssembly
            {
                  .topology                                 = vk::PrimitiveTopology::eTriangleList
                , .primitiveRestartEnable                   = VK_FALSE
            };

            vk::PipelineViewportStateCreateInfo             viewportState
            {
                  .viewportCount                            = 1
                , .scissorCount                             = 1
            };

            vk::PipelineRasterizationStateCreateInfo        rasterizer
            {
                  .depthClampEnable                         = VK_FALSE
                , .rasterizerDiscardEnable                  = VK_FALSE
                , .polygonMode                              = vk::PolygonMode::eFill
                , .cullMode                                 = vk::CullModeFlagBits::eBack
                , .frontFace                                = vk::FrontFace::eCounterClockwise
                , .depthBiasEnable                          = VK_FALSE
                , .lineWidth                                = 1.0f
            };

            vk::PipelineMultisampleStateCreateInfo          multisampling
            {
                  .rasterizationSamples                     = msaaSamples
                , .sampleShadingEnable                      = VK_TRUE
		        , .minSampleShading			                = 0.2f
            };

            vk::PipelineDepthStencilStateCreateInfo         depthStencil
            {
                  .depthTestEnable                          = VK_TRUE
                , .depthWriteEnable                         = VK_TRUE
                , .depthCompareOp                           = vk::CompareOp::eLess
                , .depthBoundsTestEnable                    = VK_FALSE
                , .stencilTestEnable                        = VK_FALSE
            };

            vk::PipelineColorBlendAttachmentState           colorBlendAttachment
            {
                  .blendEnable                              = VK_FALSE
                , .colorWriteMask                           =       vk::ColorComponentFlagBits::eR
                                                                |   vk::ColorComponentFlagBits::eG
                                                                |   vk::ColorComponentFlagBits::eB
                                                                |   vk::ColorComponentFlagBits::eA
            };

            vk::PipelineColorBlendStateCreateInfo           colorBlending
            {
                  .logicOpEnable                            = VK_FALSE
                , .logicOp                                  = vk::LogicOp::eCopy
                , .attachmentCount                          = 1
                , .pAttachments                             = &colorBlendAttachment
            };

            std::vector<vk::DynamicState>     dynamicStates = 
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
                  .setLayoutCount                           = 1
                , .pSetLayouts                              = &*descriptorSetLayout
            };

            pipelineLayout                                  = device.createPipelineLayout(pipelineLayoutInfo);

            // Configure pipeline based on whether we're using the KHR roadmap 2022 profile
            // With the KHR roadmap 2022 profile, we can use dynamic rendering
            vk::StructureChain<  vk::GraphicsPipelineCreateInfo
                               , vk::PipelineRenderingCreateInfo> pipelineCreateInfoChain =
            {
                {
                      .stageCount                           = 2
                    , .pStages                              = shaderStages
                    , .pVertexInputState                    = &vertexInputInfo
                    , .pInputAssemblyState                  = &inputAssembly
                    , .pViewportState                       = &viewportState
                    , .pRasterizationState                  = &rasterizer
                    , .pMultisampleState                    = &multisampling
                    , .pDepthStencilState                   = &depthStencil
                    , .pColorBlendState                     = &colorBlending
                    , .pDynamicState                        = &dynamicState
                    , .layout                               = pipelineLayout
                    , .renderPass                           = nullptr  
                },
                {
                      .colorAttachmentCount                 = 1
                    , .pColorAttachmentFormats              = &swapChainSurfaceFormat.format
                    , .depthAttachmentFormat                = findDepthFormat()
                }
            };
	    
	    if (appInfo.profileSupported)
	    {
		    std::cout << "Creating pipeline with dynamic rendering (KHR roadmap 2022 profile)" << std::endl;
	    }
	    else
	    {
            std::cout << "Creating pipeline with traditional render pass (fallback)" << std::endl;
            pipelineCreateInfoChain.unlink<vk::PipelineRenderingCreateInfo>();
            pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>().renderPass = *renderPass;
	    }

            graphicsPipeline = vk::raii::Pipeline(  device
                                                  , nullptr
                                                  , pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
        }
        

//******************************************************************************************
// 
//  Name:           createFramebuffers
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void createFramebuffers()
        {
            // This is only called if the Best Practices profile is not supported
            // or if dynamic rendering is not available
            swapChainFramebuffers.reserve(swapChainImageViews.size());
            
            for (size_t i = 0; i < swapChainImageViews.size(); i++)
            {
                std::array<vk::ImageView, 3> attachments = 
                {
                      *colorImageView
                    , *depthImageView
                    , *swapChainImageViews[i]
                };
                
                vk::FramebufferCreateInfo framebufferInfo
                {
                      .renderPass		                    = *renderPass
                    , .attachmentCount	                    = static_cast<uint32_t>(attachments.size())
                    , .pAttachments		                    = attachments.data()
                    , .width		                        = swapChainExtent.width
                    , .height		                        = swapChainExtent.height
                    , .layers		                        = 1
                };
                
                swapChainFramebuffers.push_back(device.createFramebuffer(framebufferInfo));
            }
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
            vk::CommandPoolCreateInfo   poolInfo
            {
                  .flags                                    = vk::CommandPoolCreateFlagBits::eResetCommandBuffer
                , .queueFamilyIndex                         = queueIndex
            };
            commandPool = vk::raii::CommandPool(  device
                                                , poolInfo
                                               );
        }
        

//******************************************************************************************
// 
//  Name:           createColorResources
//  Arguments:      N/A
//  Returns:        
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void createColorResources()
        {
            vk::Format colorFormat                          = swapChainSurfaceFormat.format;

            createImage(
                  swapChainExtent.width
                , swapChainExtent.height
                , 1
                , msaaSamples
                , colorFormat
                , vk::ImageTiling::eOptimal
                , vk::ImageUsageFlagBits::eTransientAttachment
                | vk::ImageUsageFlagBits::eColorAttachment
                , vk::MemoryPropertyFlagBits::eDeviceLocal
                , colorImage
                , colorImageMemory
            );

            colorImageView                                  = createImageView(
                                                                                  colorImage
                                                                                , colorFormat
                                                                                , vk::ImageAspectFlagBits::eColor
                                                                                , 1
                                                                            );
        }
        

//******************************************************************************************
// 
//  Name:           createDepthResources
//  Arguments:      N/A
//  Returns:        
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void createDepthResources()
        {
            vk::Format                  depthFormat         = findDepthFormat();

            createImage(
                  swapChainExtent.width
                , swapChainExtent.height
                , 1
                , msaaSamples
                , depthFormat
                , vk::ImageTiling::eOptimal
                , vk::ImageUsageFlagBits::eDepthStencilAttachment
                , vk::MemoryPropertyFlagBits::eDeviceLocal
                , depthImage
                , depthImageMemory
            );

            depthImageView                                  = createImageView( 
                                                                                depthImage
                                                                              , depthFormat
                                                                              , vk::ImageAspectFlagBits::eDepth
                                                                              , 1
                                                                            );
        }
        

//******************************************************************************************
// 
//  Name:           findSupportedformat
//  Arguments:      N/A
//  Returns:        
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        vk::Format findSupportedFormat(
              const std::vector<vk::Format>  &candidates
            , vk::ImageTiling                tiling
            , vk::FormatFeatureFlags         features
        )
        {
            for (vk::Format format : candidates)
            {
                vk::FormatProperties        props           = physicalDevice.getFormatProperties(format);

                if (   tiling == vk::ImageTiling::eLinear
                    && (props.linearTilingFeatures & features) == features)
                {
                    return format;
                }
                if (   tiling == vk::ImageTiling::eOptimal
                    && (props.optimalTilingFeatures & features) == features)
                {
                    return format;
                }
            }

            throw std::runtime_error("Failed to find supported format!");
        }
        

//******************************************************************************************
// 
//  Name:           findDepthFormat
//  Arguments:      N/A
//  Returns:        vk::Format
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        vk::Format findDepthFormat()
        {
            return findSupportedFormat
            (
                {
                      vk::Format::eD32Sfloat
                    , vk::Format::eD32SfloatS8Uint
                    , vk::Format::eD24UnormS8Uint
                }
                , vk::ImageTiling::eOptimal
                , vk::FormatFeatureFlagBits::eDepthStencilAttachment
            );
        }
        

//******************************************************************************************
// 
//  Name:           hasStencilComponent
//  Arguments:      N/A
//  Returns:        bool
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        static bool hasStencilComponent(vk::Format format)
        {
            return    format == vk::Format::eD32SfloatS8Uint 
                   || format == vk::Format::eD24UnormS8Uint;
        }
        

//******************************************************************************************
// 
//  Name:           createTextureImage
//  Arguments:      N/A
//  Returns:        
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void createTextureImage()
        {
            int                         texWidth
                                      , texHeight
                                      , texChannels;

            stbi_uc                     *pixels             = stbi_load(  TEXTURE_PATH.c_str()
                                                                        , &texWidth
                                                                        , &texHeight
                                                                        , &texChannels
                                                                        , STBI_rgb_alpha);

            vk::DeviceSize              imageSize           = texWidth * texHeight * 4;

            mipLevels                                       = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;

            if (!pixels)
            {
                throw std::runtime_error("Failed to load texture image!");
            }

            vk::raii::Buffer            stagingBuffer({});
            vk::raii::DeviceMemory      stagingBufferMemory({});

            createBuffer(
                  imageSize
                , vk::BufferUsageFlagBits::eTransferSrc
                , vk::MemoryPropertyFlagBits::eHostVisible
                | vk::MemoryPropertyFlagBits::eHostCoherent
                , stagingBuffer
                , stagingBufferMemory
            );

            void                        *data               = stagingBufferMemory.mapMemory(  0
                                                                                            , imageSize);

            memcpy(  
                  data
                , pixels
                , imageSize
            );

            stagingBufferMemory.unmapMemory();

            stbi_image_free(pixels);

            createImage(  
                  texWidth
                , texHeight
                , mipLevels
                , vk::SampleCountFlagBits::e1
                , vk::Format::eR8G8B8A8Srgb
                , vk::ImageTiling::eOptimal
                , vk::ImageUsageFlagBits::eTransferSrc
                | vk::ImageUsageFlagBits::eTransferDst
                | vk::ImageUsageFlagBits::eSampled
                , vk::MemoryPropertyFlagBits::eDeviceLocal
                , textureImage
                , textureImageMemory
            );

            transitionImageLayout(  
                  textureImage
                , vk::ImageLayout::eUndefined
                , vk::ImageLayout::eTransferDstOptimal
                , mipLevels
            );

            copyBufferToImage(  
                  stagingBuffer
                , textureImage
                , static_cast<uint32_t>(texWidth)
                , static_cast<uint32_t>(texHeight)
            );
            
            generateMipmaps(
                  textureImage
                , vk::Format::eR8G8B8A8Srgb
                , texWidth
                , texHeight
                , mipLevels
            );
        }
        

//******************************************************************************************
// 
//  Name:           generateMipmaps
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void generateMipmaps(
              vk::raii::Image &image
            , vk::Format imageFormat
            , int32_t texWidth
            , int32_t texHeight
            , uint32_t mipLevels
        )
        {
            // Check if image format supports linear blit-ing
            vk::FormatProperties formatProperties           = physicalDevice.getFormatProperties(imageFormat);

            if (!(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear))
            {
                throw std::runtime_error("Texture image format does not support linear blitting!");
            }

            std::unique_ptr<vk::raii::CommandBuffer> commandBuffer = beginSingleTimeCommands();

            vk::ImageMemoryBarrier          barrier         =
            {
                  .srcAccessMask                            = vk::AccessFlagBits::eTransferWrite
                , .dstAccessMask                            = vk::AccessFlagBits::eTransferRead
                , .oldLayout                                = vk::ImageLayout::eTransferDstOptimal
                , .newLayout                                = vk::ImageLayout::eTransferSrcOptimal
                , .srcQueueFamilyIndex                      = vk::QueueFamilyIgnored
                , .dstQueueFamilyIndex                      = vk::QueueFamilyIgnored
                , .image                                    = image
            };

            barrier.subresourceRange.aspectMask             = vk::ImageAspectFlagBits::eColor;
            barrier.subresourceRange.baseArrayLayer         = 0;
            barrier.subresourceRange.layerCount             = 1;
            barrier.subresourceRange.levelCount             = 1;

            int32_t                         mipWidth        = texWidth;
            int32_t                         mipHeight       = texHeight;

            for (uint32_t i = 1; i < mipLevels; i++)
            {
                barrier.subresourceRange.baseMipLevel       = i - 1;
                barrier.oldLayout                           = vk::ImageLayout::eTransferDstOptimal;
                barrier.newLayout                           = vk::ImageLayout::eTransferSrcOptimal;
                barrier.srcAccessMask                       = vk::AccessFlagBits::eTransferWrite;
                barrier.dstAccessMask                       = vk::AccessFlagBits::eTransferRead;

                commandBuffer->pipelineBarrier(
                      vk::PipelineStageFlagBits::eTransfer
                    , vk::PipelineStageFlagBits::eTransfer
                    , {}
                    , {}
                    , {}
                    , barrier
                );

                vk::ArrayWrapper1D<vk::Offset3D, 2> offsets, dstOffsets;

                offsets[0]                                  = vk::Offset3D(0, 0, 0);
                offsets[1]                                  = vk::Offset3D(mipWidth, mipHeight, 1);
                dstOffsets[0]                               = vk::Offset3D(0, 0, 0);
                dstOffsets[1]                               = vk::Offset3D(  mipWidth > 1  ? mipWidth / 2  : 1
                                                                           , mipHeight > 1 ? mipHeight / 2 : 1
                                                                           , 1
                                                                          );

                vk::ImageBlit blit                          = 
                {
                      .srcSubresource                       = {}
                    , .srcOffsets                           = offsets
                    , .dstSubresource                       = {}
                    , .dstOffsets                           = dstOffsets
                };

                blit.srcSubresource                         = vk::ImageSubresourceLayers(
                                                                                           vk::ImageAspectFlagBits::eColor
                                                                                         , i - 1
                                                                                         , 0
                                                                                         , 1
                                                                                        );
                
                blit.dstSubresource                         = vk::ImageSubresourceLayers(
                                                                                           vk::ImageAspectFlagBits::eColor
                                                                                         , i
                                                                                         , 0
                                                                                         , 1
                                                                                        );
                
                commandBuffer->blitImage(  
                                           image
                                         , vk::ImageLayout::eTransferSrcOptimal
                                         , image
                                         , vk::ImageLayout::eTransferDstOptimal
                                         , {blit}
                                         , vk::Filter::eLinear
                                        );

                barrier.oldLayout                           = vk::ImageLayout::eTransferSrcOptimal;
                barrier.newLayout                           = vk::ImageLayout::eShaderReadOnlyOptimal;
                barrier.srcAccessMask                       = vk::AccessFlagBits::eTransferRead;
                barrier.dstAccessMask                       = vk::AccessFlagBits::eShaderRead;

                commandBuffer->pipelineBarrier(
                      vk::PipelineStageFlagBits::eTransfer
                    , vk::PipelineStageFlagBits::eFragmentShader
                    , {}
                    , {}
                    , {}
                    , barrier
                );

                if (mipWidth > 1)
                    mipWidth /= 2;
                if (mipHeight > 1)
                    mipHeight /= 2;
            }

            barrier.subresourceRange.baseMipLevel           = mipLevels - 1;
            barrier.oldLayout                               = vk::ImageLayout::eTransferDstOptimal;
            barrier.newLayout                               = vk::ImageLayout::eShaderReadOnlyOptimal;
            barrier.srcAccessMask                           = vk::AccessFlagBits::eTransferWrite;
            barrier.dstAccessMask                           = vk::AccessFlagBits::eShaderRead;

            commandBuffer->pipelineBarrier(
                    vk::PipelineStageFlagBits::eTransfer
                , vk::PipelineStageFlagBits::eFragmentShader
                , {}
                , {}
                , {}
                , barrier
            );

            endSingleTimeCommands(*commandBuffer);
        }
        

//******************************************************************************************
// 
//  Name:           getMaxUsableSampleCount
//  Arguments:      N/A
//  Returns:        vk::SampleCountFlagBits
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        vk::SampleCountFlagBits getMaxUsableSampleCount()
        {
            vk::PhysicalDeviceProperties                    physicalDeviceProperties
                                                            = physicalDevice.getProperties();

            vk::SampleCountFlags            counts          =   physicalDeviceProperties.limits.framebufferColorSampleCounts
                                                              & physicalDeviceProperties.limits.framebufferDepthSampleCounts;

            if (counts & vk::SampleCountFlagBits::e64)
            {
                return vk::SampleCountFlagBits::e64;
            }
            if (counts & vk::SampleCountFlagBits::e32)
            {
                return vk::SampleCountFlagBits::e32;
            }
            if (counts & vk::SampleCountFlagBits::e16)
            {
                return vk::SampleCountFlagBits::e16;
            }
            if (counts & vk::SampleCountFlagBits::e8)
            {
                return vk::SampleCountFlagBits::e8;
            }
            if (counts & vk::SampleCountFlagBits::e4)
            {
                return vk::SampleCountFlagBits::e4;
            }
            if (counts & vk::SampleCountFlagBits::e2)
            {
                return vk::SampleCountFlagBits::e2;
            }

            return vk::SampleCountFlagBits::e1;
        }
        

//******************************************************************************************
// 
//  Name:           createTextureImageView
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void createTextureImageView()
        {
            textureImageView                                = createImageView(  textureImage
                                                                              , vk::Format::eR8G8B8A8Srgb
                                                                              , vk::ImageAspectFlagBits::eColor
                                                                              , mipLevels
                                                                             );
        }
        

//******************************************************************************************
// 
//  Name:           createTextureSampler
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void createTextureSampler()
        {
            vk::PhysicalDeviceProperties        properties  = physicalDevice.getProperties();

            vk::SamplerCreateInfo               samplerInfo
            {
                  .magFilter                                = vk::Filter::eLinear
                , .minFilter                                = vk::Filter::eLinear
                , .mipmapMode                               = vk::SamplerMipmapMode::eLinear
                , .addressModeU                             = vk::SamplerAddressMode::eRepeat
                , .addressModeV                             = vk::SamplerAddressMode::eRepeat
                , .addressModeW                             = vk::SamplerAddressMode::eRepeat
                , .mipLodBias                               = 0.0f
                , .anisotropyEnable                         = vk::True
                , .maxAnisotropy                            = properties.limits.maxSamplerAnisotropy
                , .compareEnable                            = vk::False
                , .compareOp                                = vk::CompareOp::eAlways
            };

            textureSampler                                  = vk::raii::Sampler(  device
                                                                                , samplerInfo
                                                                               );
        }
        

//******************************************************************************************
// 
//  Name:           createImageView
//  Arguments:      N/A
//  Returns:        vk::raii::ImageView
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        [[nodiscard]] vk::raii::ImageView createImageView(
              const vk::raii::Image &image
            , vk::Format format
            , vk::ImageAspectFlags aspectFlags
            , uint32_t mipLevels
        ) const
        {
            vk::ImageViewCreateInfo     viewInfo
            {
                  .image                                    = image
                , .viewType                                 = vk::ImageViewType::e2D
                , .format                                   = format
                , .subresourceRange                         = 
                {
                      aspectFlags
                    , 0
                    , mipLevels
                    , 0
                    , 1
                }
            };

            return vk::raii::ImageView(  device
                                       , viewInfo);
        }
        

//******************************************************************************************
// 
//  Name:           createImage
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void createImage(
              uint32_t                  width
            , uint32_t                  height
            , uint32_t                  mipLevels
            , vk::SampleCountFlagBits   numSamples
            , vk::Format                format
            , vk::ImageTiling           tiling
            , vk::ImageUsageFlags       usage
            , vk::MemoryPropertyFlags   properties
            , vk::raii::Image           &image
            , vk::raii::DeviceMemory    &imageMemory
        )
        {
            vk::ImageCreateInfo             imageInfo
            {
                  .imageType                                = vk::ImageType::e2D
                , .format                                   = format
                , .extent                                   = { width, height, 1 }
                , .mipLevels                                = mipLevels
                , .arrayLayers                              = 1
                , .samples                                  = numSamples
                , .tiling                                   = tiling
                , .usage                                    = usage
                , .sharingMode                              = vk::SharingMode::eExclusive
                , .initialLayout                            = vk::ImageLayout::eUndefined
            };

            image                                           = vk::raii::Image(  device
                                                                              , imageInfo
                                                                             );
            
            vk::MemoryRequirements          memRequirements = image.getMemoryRequirements();
            vk::MemoryAllocateInfo          allocInfo
            {
                  .allocationSize                           = memRequirements.size
                , .memoryTypeIndex                          = findMemoryType(  memRequirements.memoryTypeBits
                                                                             , properties
                                                                            )
            };
            imageMemory                                     = vk::raii::DeviceMemory(  device
                                                                                     , allocInfo
                                                                                    );
            image.bindMemory(  imageMemory
                             , 0
                            );
        }
        

//******************************************************************************************
// 
//  Name:           transitionImageLayout
//  Arguments:      N/A
//  Returns:        
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void transitionImageLayout(
              const vk::raii::Image         &image
            , const vk::ImageLayout               oldLayout
            , const vk::ImageLayout               newLayout
            , uint32_t                      mipLevels
        )
        {
            const auto                      commandBuffer   = beginSingleTimeCommands();

            vk::ImageMemoryBarrier          barrier         
            {
                  .oldLayout                                = oldLayout
                , .newLayout                                = newLayout
                , .image                                    = image
                , .subresourceRange                         = 
                {
                      vk::ImageAspectFlagBits::eColor
                    , 0
                    , mipLevels
                    , 0
                    , 1
                }
            };

            vk::PipelineStageFlags sourceStage;
            vk::PipelineStageFlags destinationStage;

            if (   oldLayout == vk::ImageLayout::eUndefined 
                && newLayout == vk::ImageLayout::eTransferDstOptimal)
            {
                barrier.srcAccessMask                       = {};
                barrier.dstAccessMask                       = vk::AccessFlagBits::eTransferWrite;

                sourceStage                                 = vk::PipelineStageFlagBits::eTopOfPipe;
                destinationStage                            = vk::PipelineStageFlagBits::eTransfer;
            }
            else if (   oldLayout == vk::ImageLayout::eTransferDstOptimal
                     && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal)
            {
                barrier.srcAccessMask                       = vk::AccessFlagBits::eTransferWrite;
                barrier.dstAccessMask                       = vk::AccessFlagBits::eShaderRead;

                sourceStage                                 = vk::PipelineStageFlagBits::eTransfer;
                destinationStage                            = vk::PipelineStageFlagBits::eFragmentShader;
            }
            else
            {
                throw std::invalid_argument("Unsupported layout transition!");
            }

            commandBuffer->pipelineBarrier(  sourceStage
                                           , destinationStage
                                           , {}
                                           , {}
                                           , nullptr
                                           , barrier
                                          );

            endSingleTimeCommands(*commandBuffer);
        }
        

//******************************************************************************************
// 
//  Name:           copyBufferToImage
//  Arguments:      N/A
//  Returns:        
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void copyBufferToImage(
              const vk::raii::Buffer    &buffer
            , const vk::raii::Image     &image
            , uint32_t                  width
            , uint32_t                  height
        )
        {
            std::unique_ptr<vk::raii::CommandBuffer>        commandBuffer = beginSingleTimeCommands();
            vk::BufferImageCopy                             region
            {
                  .bufferOffset                             = 0
                , .bufferRowLength                          = 0
                , .bufferImageHeight                        = 0
                , .imageSubresource                         =
                {
                      vk::ImageAspectFlagBits::eColor
                    , 0
                    , 0
                    , 1
                }
                , .imageOffset                              = { 0, 0, 0 }
                , .imageExtent                              = { width, height, 1}
            };

            commandBuffer->copyBufferToImage(
                                               buffer
                                             , image
                                             , vk::ImageLayout::eTransferDstOptimal
                                             , {region}
                                            );

            endSingleTimeCommands(*commandBuffer);
        }
        

//******************************************************************************************
// 
//  Name:           loadModel
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void loadModel()
        {
            tinyobj::attrib_t                   attrib;
            std::vector<tinyobj::shape_t>       shapes;
            std::vector<tinyobj::material_t>    materials;
            std::string                         warn, err;

            if (!LoadObj(
                           &attrib
                         , &shapes
                         , &materials
                         , &warn
                         , &err
                         , MODEL_PATH.c_str()
                        )
                )
            {
                throw std::runtime_error(warn + err);
            }

            std::unordered_map<Vertex, uint32_t> uniqueVertices{};

            for (const auto &shape : shapes)
            {
                for (const auto &index : shape.mesh.indices)
                {
                    Vertex vertex{};

                    vertex.pos = 
                    {
                          attrib.vertices[3 * index.vertex_index + 0]
                        , attrib.vertices[3 * index.vertex_index + 1]
                        , attrib.vertices[3 * index.vertex_index + 2]
                    };

                    vertex.texCoord =
                    {
                          attrib.texcoords[2 * index.texcoord_index + 0]
                        , 1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
                    };

                    vertex.color = { 1.0f, 1.0f, 1.0f};

                    if (!uniqueVertices.contains(vertex))
                    {
                        uniqueVertices[vertex]              = (static_cast<uint32_t>(vertices.size()));
                        vertices.push_back(vertex);
                    }

                    indices.push_back(uniqueVertices[vertex]);
                }
            }
        }
        

//******************************************************************************************
// 
//  Name:           createVertexBuffer
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void createVertexBuffer()
        {
            vk::DeviceSize          bufferSize              = sizeof(vertices[0]) * vertices.size();
            vk::raii::Buffer        stagingBuffer({});
            vk::raii::DeviceMemory  stagingBufferMemory({});

            createBuffer(
                           bufferSize
                         , vk::BufferUsageFlagBits::eTransferSrc
                         , vk::MemoryPropertyFlagBits::eHostVisible
                         | vk::MemoryPropertyFlagBits::eHostCoherent
                         , stagingBuffer
                         , stagingBufferMemory
                        );

            void *dataStaging                               = stagingBufferMemory.mapMemory(0, bufferSize);

            memcpy(
                     dataStaging
                   , vertices.data()
                   , bufferSize
                  );

            stagingBufferMemory.unmapMemory();

            createBuffer(
                           bufferSize
                         , vk::BufferUsageFlagBits::eTransferDst
                         | vk::BufferUsageFlagBits::eVertexBuffer 
                         , vk::MemoryPropertyFlagBits::eDeviceLocal
                         , vertexBuffer
                         , vertexBufferMemory
                        );

            copyBuffer(
                         stagingBuffer
                       , vertexBuffer
                       , bufferSize
                      );
        }
        

//******************************************************************************************
// 
//  Name:           createIndexBuffer
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void createIndexBuffer()
        {
            vk::DeviceSize          bufferSize              = sizeof(indices[0]) * indices.size();

            vk::raii::Buffer        stagingBuffer({});
            vk::raii::DeviceMemory  stagingBufferMemory({});

            createBuffer(
                  bufferSize
                , vk::BufferUsageFlagBits::eTransferSrc
                , vk::MemoryPropertyFlagBits::eHostVisible
                | vk::MemoryPropertyFlagBits::eHostCoherent
                , stagingBuffer
                , stagingBufferMemory
            );

            void                *data                       = stagingBufferMemory.mapMemory(  0
                                                                                            , bufferSize);

            memcpy(  data
                   , indices.data()
                   , bufferSize);

            stagingBufferMemory.unmapMemory();

            createBuffer(  
                  bufferSize
                , vk::BufferUsageFlagBits::eTransferDst
                | vk::BufferUsageFlagBits::eIndexBuffer
                , vk::MemoryPropertyFlagBits::eDeviceLocal
                , indexBuffer
                , indexBufferMemory
            );

            copyBuffer(  stagingBuffer
                       , indexBuffer
                       , bufferSize);
        }
        

//******************************************************************************************
// 
//  Name:           createUniformBuffers
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void createUniformBuffers()
        {
            uniformBuffers.clear();
            uniformBuffersMemory.clear();
            uniformBuffersMapped.clear();

            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            {
                vk::DeviceSize          bufferSize          = sizeof(UniformBufferObject);
                vk::raii::Buffer        buffer({});
                vk::raii::DeviceMemory  bufferMem({});

                createBuffer(
                      bufferSize
                    , vk::BufferUsageFlagBits::eUniformBuffer
                    , vk::MemoryPropertyFlagBits::eHostVisible
                    | vk::MemoryPropertyFlagBits::eHostCoherent
                    , buffer
                    , bufferMem
                );

                uniformBuffers.emplace_back(std::move(buffer));

                uniformBuffersMemory.emplace_back(std::move(bufferMem));

                uniformBuffersMapped.emplace_back(uniformBuffersMemory[i].mapMemory(0, bufferSize));
            }
        }


//******************************************************************************************
// 
//  Name:           createDscriptorPool
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void createDescriptorPool()
        {
            std::array poolSize
            {
                vk::DescriptorPoolSize(
                                        vk::DescriptorType::eUniformBuffer
                                      , MAX_FRAMES_IN_FLIGHT
                )
                , vk::DescriptorPoolSize(
                                        vk::DescriptorType::eCombinedImageSampler
                                      , MAX_FRAMES_IN_FLIGHT
                )
            };

            vk::DescriptorPoolCreateInfo    poolInfo
            {
                  .flags                                    = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet
                , .maxSets                                  = MAX_FRAMES_IN_FLIGHT
                , .poolSizeCount                            = static_cast<uint32_t>(poolSize.size())
                , .pPoolSizes                               = poolSize.data()
            };

            descriptorPool                                  = vk::raii::DescriptorPool(device, poolInfo);
        }


//******************************************************************************************
// 
//  Name:           createDescriptorSets
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void createDescriptorSets()
        {
            std::vector<vk::DescriptorSetLayout>    layouts(  MAX_FRAMES_IN_FLIGHT
                                                            , descriptorSetLayout);
            vk::DescriptorSetAllocateInfo           allocInfo
            {
                  .descriptorPool                           = descriptorPool
                , .descriptorSetCount                       = static_cast<uint32_t>(layouts.size())
                , .pSetLayouts                              = layouts.data()
            };

            descriptorSets.clear();

            descriptorSets                                  = device.allocateDescriptorSets(allocInfo);

            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            {
                vk::DescriptorBufferInfo            bufferInfo
                {
                      .buffer                               = uniformBuffers[i]
                    , .offset                               = 0
                    , .range                                = sizeof(UniformBufferObject)
                };

                vk::DescriptorImageInfo             imageInfo
                {
                      .sampler                              = textureSampler
                    , .imageView                            = textureImageView
                    , .imageLayout                          = vk::ImageLayout::eShaderReadOnlyOptimal
                };

                std::array                          descriptorWrites
                {
                    vk::WriteDescriptorSet
                    {
                          .dstSet                               = descriptorSets[i]
                        , .dstBinding                           = 0
                        , .dstArrayElement                      = 0
                        , .descriptorCount                      = 1
                        , .descriptorType                       = vk::DescriptorType::eUniformBuffer
                        , .pBufferInfo                          = &bufferInfo
                    }
                    , vk::WriteDescriptorSet 
                    {
                          .dstSet                               = descriptorSets[i]
                        , .dstBinding                           = 1
                        , .dstArrayElement                      = 0
                        , .descriptorCount                      = 1
                        , .descriptorType                       = vk::DescriptorType::eCombinedImageSampler
                        , .pImageInfo                           = &imageInfo
                    }
                };

                device.updateDescriptorSets(  descriptorWrites
                                            , {});
            }
        }
        

//******************************************************************************************
// 
//  Name:           createBuffer
//  Arguments:      N/A
//  Returns:        
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void createBuffer(  
              vk::DeviceSize            size
            , vk::BufferUsageFlags      usage
            , vk::MemoryPropertyFlags   properties
            , vk::raii::Buffer          &buffer
            , vk::raii::DeviceMemory    &bufferMemory
        )
        {
            vk::BufferCreateInfo bufferInfo
            {
                  .size                                     = size
                , .usage                                    = usage
                , .sharingMode                              = vk::SharingMode::eExclusive
            };

            buffer                                          = vk::raii::Buffer(device, bufferInfo);

            vk::MemoryRequirements  memRequirements         = buffer.getMemoryRequirements();

            vk::MemoryAllocateInfo  allocInfo
            {
                  .allocationSize                           = memRequirements.size
                , .memoryTypeIndex                          = findMemoryType(
                                                                               memRequirements.memoryTypeBits
                                                                             , properties
                                                                            )
            };

            bufferMemory            = vk::raii::DeviceMemory(device, allocInfo);
            
            buffer.bindMemory(bufferMemory, 0);
        }


//******************************************************************************************
// 
//  Name:           beginSingleTimeCommands
//  Arguments:      N/A
//  Returns:        std::unique_ptr<vk::raii::CommandBuffer>
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        std::unique_ptr<vk::raii::CommandBuffer> beginSingleTimeCommands()
        {
            vk::CommandBufferAllocateInfo   allocInfo
            {
                  .commandPool                              = commandPool
                , .level                                    = vk::CommandBufferLevel::ePrimary
                , .commandBufferCount                       = 1
            };

            std::unique_ptr<vk::raii::CommandBuffer>        commandBuffer 
                                                            = std::make_unique<vk::raii::CommandBuffer>(
                                                              std::move(
                                                                        vk::raii::CommandBuffers(  
                                                                              device
                                                                            , allocInfo).front()
                                                                        )
                                                              );
            vk::CommandBufferBeginInfo      beginInfo
            {
                  .flags                                    = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
            };

            commandBuffer->begin(beginInfo);

            return commandBuffer;
        }


//******************************************************************************************
// 
//  Name:           endSingleTimeCommands
//  Arguments:      vk::raii::CommandBuffer &&commandBuffer
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void endSingleTimeCommands(const vk::raii::CommandBuffer &commandBuffer) const
        {
            commandBuffer.end();

            vk::SubmitInfo                  submitInfo
            {
                  .commandBufferCount                       = 1
                , .pCommandBuffers                          = &*commandBuffer
            };

            queue.submit(  submitInfo
                         , nullptr);

            queue.waitIdle();
        }


//******************************************************************************************
// 
//  Name:           copyBuffer
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void copyBuffer(  
              vk::raii::Buffer  &srcBuffer
            , vk::raii::Buffer  &dstBuffer
            , vk::DeviceSize    size
        )
        {
            vk::CommandBufferAllocateInfo   allocInfo
            {
                  .commandPool                              = commandPool
                , .level                                    = vk::CommandBufferLevel::ePrimary
                , .commandBufferCount                       = 1
            };

            vk::raii::CommandBuffer       commandCopyBuffer = std::move(device.allocateCommandBuffers(allocInfo).front());
            
            commandCopyBuffer.begin(
                vk::CommandBufferBeginInfo
                {
                      .flags                                = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
                }
            );

            commandCopyBuffer.copyBuffer(  *srcBuffer
                                         , *dstBuffer
                                         , vk::BufferCopy{.size = size});

            commandCopyBuffer.end();

            queue.submit(vk::SubmitInfo
            {
                  .commandBufferCount                       = 1
                , .pCommandBuffers                          = &*commandCopyBuffer
            }
            , nullptr);

            queue.waitIdle();
        }
        

//******************************************************************************************
// 
//  Name:           findMemoryType
//  Arguments:      N/A
//  Returns:        uint32_t
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        uint32_t findMemoryType(
              uint32_t typeFilter
            , vk::MemoryPropertyFlags properties
        )
        {
            vk::PhysicalDeviceMemoryProperties              memProperties = physicalDevice.getMemoryProperties();

            for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
            {
                if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
                {
                    return i;
                }
            }

            throw std::runtime_error("Failed to find suitable memory type!");
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
                  swapChainImages[imageIndex]
                , vk::ImageLayout::eUndefined
                , vk::ImageLayout::eColorAttachmentOptimal
                , {}                                                    // scrAccessMask (No need to wait for previous operations)
                , vk::AccessFlagBits2::eColorAttachmentWrite            // dstAccessMask
                , vk::PipelineStageFlagBits2::eColorAttachmentOutput    // srcStage
                , vk::PipelineStageFlagBits2::eColorAttachmentOutput    // dstStage
                , vk::ImageAspectFlagBits::eColor
            );

            // Transition the multisampled color image to COLOR_ATTACHMENT_OPTIMAL
            transition_image_layout(
                  *colorImage
                , vk::ImageLayout::eUndefined
                , vk::ImageLayout::eColorAttachmentOptimal
                , vk::AccessFlagBits2::eColorAttachmentWrite
                , vk::AccessFlagBits2::eColorAttachmentWrite
                , vk::PipelineStageFlagBits2::eColorAttachmentOutput
                , vk::PipelineStageFlagBits2::eColorAttachmentOutput
                , vk::ImageAspectFlagBits::eColor
            );

            // Transition depth image to DEPTH_ATTACHMENT_OPTIMAL
            transition_image_layout(
                  *depthImage
                , vk::ImageLayout::eUndefined
                , vk::ImageLayout::eDepthAttachmentOptimal
                , vk::AccessFlagBits2::eDepthStencilAttachmentWrite
                , vk::AccessFlagBits2::eDepthStencilAttachmentWrite
                , vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests
                , vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests
                , vk::ImageAspectFlagBits::eDepth
            );

            vk::ClearValue                  clearColor      = vk::ClearColorValue(  0.0f
                                                                                  , 0.0f
                                                                                  , 0.0f
                                                                                  , 1.0f);

            vk::ClearValue                  clearDepth      = vk::ClearDepthStencilValue(  1.0f
                                                                                         , 0);

            // Color attachment (multisampled) with resolve attachment
            vk::RenderingAttachmentInfo     colorAttachment =
            {
                  .imageView                                = colorImageView
                , .imageLayout                              = vk::ImageLayout::eColorAttachmentOptimal
                , .resolveMode                              = vk::ResolveModeFlagBits::eAverage
                , .resolveImageView                         = swapChainImageViews[imageIndex]
                , .resolveImageLayout                       = vk::ImageLayout::eColorAttachmentOptimal
                , .loadOp                                   = vk::AttachmentLoadOp::eClear
                , .storeOp                                  = vk::AttachmentStoreOp::eStore
                , .clearValue                               = clearColor
            };

            // Depth attachment
            vk::RenderingAttachmentInfo     depthAttachment =
            {
                  .imageView                                = depthImageView
                , .imageLayout                              = vk::ImageLayout::eDepthAttachmentOptimal
                , .loadOp                                   = vk::AttachmentLoadOp::eClear
                , .storeOp                                  = vk::AttachmentStoreOp::eDontCare
                , .clearValue                               = clearDepth
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
                , .pColorAttachments                        = &colorAttachment
                , .pDepthAttachment                         = &depthAttachment
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

            commandBuffer.bindVertexBuffers(  0
                                            , *vertexBuffer
                                            , {0});

            commandBuffer.bindIndexBuffer(  *indexBuffer
                                          , 0
                                          , vk::IndexType::eUint32);

            commandBuffer.bindDescriptorSets(  vk::PipelineBindPoint::eGraphics
                                             , pipelineLayout
                                             , 0
                                             , *descriptorSets[frameIndex]
                                             , nullptr);

            commandBuffer.drawIndexed(  indices.size()
                                      , 1
                                      , 0
                                      , 0
                                      , 0);
            
            commandBuffer.endRendering();

            // After rendering, transition the swapchain image to PRESENT_SRC
            transition_image_layout(
                  swapChainImages[imageIndex]
                , vk::ImageLayout::eColorAttachmentOptimal
                , vk::ImageLayout::ePresentSrcKHR
                , vk::AccessFlagBits2::eColorAttachmentWrite            // srcAccessMask
                , {}                                                    // dstAccessMask
                , vk::PipelineStageFlagBits2::eColorAttachmentOutput    // srcStage
                , vk::PipelineStageFlagBits2::eBottomOfPipe             // dstStage
                , vk::ImageAspectFlagBits::eColor
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
              vk::Image                     image
            , vk::ImageLayout               old_layout
            , vk::ImageLayout               new_layout
            , vk::AccessFlags2              src_access_mask
            , vk::AccessFlags2              dst_access_mask
            , vk::PipelineStageFlags2       src_stage_mask
            , vk::PipelineStageFlags2       dst_stage_mask
            , vk::ImageAspectFlags          image_aspect_flags
        )
        {
            vk::ImageMemoryBarrier2         barrier         = 
            {
                  .srcStageMask                             = src_stage_mask
                , .srcAccessMask                            = src_access_mask
                , .dstStageMask                             = dst_stage_mask
                , .dstAccessMask                            = dst_access_mask
                , .oldLayout                                = old_layout
                , .newLayout                                = new_layout
                , .srcQueueFamilyIndex                      = VK_QUEUE_FAMILY_IGNORED
                , .dstQueueFamilyIndex                      = VK_QUEUE_FAMILY_IGNORED
                , .image                                    = image
                , .subresourceRange                         = 
                {
                      .aspectMask                           = image_aspect_flags
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
//  Name:           updateUniformBuffer
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void updateUniformBuffer(uint32_t currentImage) const
        {
            static auto             startTime               = std::chrono::high_resolution_clock::now();

            auto                    currentTime             = std::chrono::high_resolution_clock::now();
            float                   time                    = std::chrono::duration<float>(currentTime - startTime).count();

            UniformBufferObject     ubo{};

            ubo.model                                       = rotate(  glm::mat4(1.0f)
                                                                     , time * glm::radians(90.0f)
                                                                     , glm::vec3(0.0f, 0.0f, 1.0f));
            
            ubo.view                                        = lookAt(  glm::vec3(2.0f, 2.0f, 2.0f)
                                                                     , glm::vec3(0.0f, 0.0f, 0.0f)
                                                                     , glm::vec3(0.0f, 0.0f, 1.0f));

            ubo.proj                                        = glm::perspective(  glm::radians(45.0f)
                                                                               , static_cast<float>(swapChainExtent.width) / static_cast<float>(swapChainExtent.height)
                                                                               , 0.1f
                                                                               , 10.0f);

            ubo.proj[1][1] *= -1;

            memcpy(  uniformBuffersMapped[currentImage]
                   , &ubo
                   , sizeof(ubo));
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

            updateUniformBuffer(frameIndex);

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
              .codeSize     = code.size()
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
//  Returns:        uint32_t
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
                                       , capabilities.maxImageExtent.height)
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
        
        [[nodiscard]] std::vector<const char *> getRequiredInstanceExtensions()
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

        static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(  
              vk::DebugUtilsMessageSeverityFlagBitsEXT severity
            , vk::DebugUtilsMessageTypeFlagsEXT type
            , const vk::DebugUtilsMessengerCallbackDataEXT *pCallbackData
            , void *
        )
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