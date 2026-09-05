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
#if defined(__ANDROID__)
#	include <vulkan/vulkan_android.h>
#	include <vulkan/vulkan_core.h>
#endif
//#include <vulkan/vulkan_profiles.hpp>
#include "/home/nik/vulkanSDK/1.4.350.1/x86_64/include/vulkan/vulkan_profiles.hpp"

#if defined(__ANDROID__)
#	define PLATFORM_ANDROID 1
#else
#	define PLATFORM_DESKTOP 1
#endif

// Include tinygltf instead of tinyobjloader
// TINYGLTF_IMPLEMENTATION is already defined in the command line
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

// Include KTX library for texture loading
#include <ktx.h>

#if PLATFORM_ANDROID
#	include <android/asset_manager.h>
#	include <android/asset_manager_jni.h>
#	include <android/log.h>
#	include <game-activity/native_app_glue/android_native_app_glue.h>

// Declare and implement app_dummy function from native_app_glue
extern "C" void app_dummy()
{
	// This is a dummy function that does nothing
	// It's used to prevent the linker from stripping out the native_app_glue code
}

// Define AAssetManager type for Android
typedef AAssetManager AssetManagerType;

#	define LOGI(...) ((void) __android_log_print(ANDROID_LOG_INFO, "VulkanTutorial", __VA_ARGS__))
#	define LOGW(...) ((void) __android_log_print(ANDROID_LOG_WARN, "VulkanTutorial", __VA_ARGS__))
#	define LOGE(...) ((void) __android_log_print(ANDROID_LOG_ERROR, "VulkanTutorial", __VA_ARGS__))
#else
// Define AAssetManager type for non-Android platforms
typedef void AssetManagerType;

// Desktop-specific includes
#	define GLFW_INCLUDE_VULKAN        // REQUIRED only for GLFW CreateWindowSurface.
#include <GLFW/glfw3.h>

// Define logging macros for Desktop
#	define LOGI(...)	\
		printf(__VA_ARGS__); \
		printf("\n")
#	define LOGW(...)	\
		printf(__VA_ARGS__); \
		printf("\n")
#	define LOGE(...)		\
		fprintf(stderr, __VA_ARGS__); \
		fprintf(stderr, "\n")
#endif

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#define GLM_FORCE_CXX11
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

constexpr uint32_t                  WIDTH                   = 800;
constexpr uint32_t                  HEIGHT                  = 600;
// Update paths to use glTF model and KTX2 texture
const std::string                   MODEL_PATH              = "models/viking_room.glb";
const std::string                   TEXTURE_PATH            = "textures/viking_room.ktx2";
constexpr int                       MAX_FRAMES_IN_FLIGHT    = 2;

// Define VpProfileProperties structure for Android only
#if PLATFORM_ANDROID
#	ifndef VP_PROFILE_PROPERTIES_DEFINED
#		define VP_PROFILE_PROPERTIES_DEFINED
struct VpProfileProperties
{
	char		name[256];
	uint32_t	specVersion;
};
#	endif
#endif

// Define Vulkan Profile constants
#	ifndef VP_KHR_ROADMAP_2022_NAME
#		define VP_KHR_ROADMAP_2022_NAME "VP_KHR_roadmap_2022"
#	endif

#	ifndef VP_KHR_ROADMAP_2022_SPEC_VERSION
#		define VP_KHR_ROADMAP_2022_SPEC_VERSION 1
#	endif

struct AppInfo
{
	bool			profileSupported		= false;
	VpProfileProperties	profile;
};

#if PLATFORM_ANDROID
void android_main(android_app *app);

struct AndroidAppState
{
	ANativeWindow 		*nativeWindow			= nullptr;
	bool			initialized			= false;
	android_app		*app				= nullptr;
};
#endif

#ifdef NDEBUG
constexpr bool			enableValidationLayers		= false;
#else
constexpr bool			enableValidationLayers		= true;
#endif

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


// Cross-platform application class
class VulkanApplication
{
    public:
#if PLATFORM_ANDROID
	void run(android_app *app)
	{
		androidAppState.nativeWindow		= app->window;
		androidAppState.app			= app;
		app->userData				= &androidAppState;
		app->onAppCmd				= handleAppCommand;
		// Note: onInputEvent is no longer a member of android_app in the current NDK version
		// Input events are now handled differently
		
		int					events;
		android_poll_source 			*source;
		
		while (app->destroyRequested == 0)
		{
			while (ALooper_pollOnce(
				androidAppState.initialized ? 0 : -1
				, nullptr
				, &events
				, (void **) &source) >= 0)
			{
				if (source != nullptr)
				{
					source->process(app, source);
				}
			}

			if (androidAppState.initialized && androidAppState.nativeWindow != nullptr)
			{
				drawFrame();
			}
		}

		if (androidAppState.initialized)
		{
			device.waitIdle();
		}
	}
#else


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
#endif

    private:

#if PLATFORM_ANDROID
	AndroidAppState 			androidAppState;
	
	static void handleAppCommand(
		android_app *app
		, int32_t cmd
	)
	{
		auto *appState			= static_cast<AndroidAppState *>(app->userData);
		
		switch (cmd)
		{
			case APP_CMD_INIT_WINDOW:
				if (app->window != nullptr)
				{
					appState->nativeWindow		= app->window;
					// We can't cast AndroidAppState to VulkanApplication directly
					// Instead, we need to access the VulkanApplication instance through a global variable
					// or another mechanism. For now, we'll just set the initialized flag.
					appState->initialized		= true;
				}
				break;
			case APP_CMD_TERM_WINDOW:
				appState->nativeWindow 			= nullptr;
				break;
			default:
				break;
				}
		}
		
		static int32_t handleInputEvent(
			android_app *app
			, AInputEvent *event
		)
		{
			if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION)
			{
				float x					= AMotionEvent_getX(event, 0);
				float y					= AMotionEvent_getY(event, 0);
				
				LOGI("Touch at: %f, %f", x, y);
				
				return 1;
			}
		return 0;
	}
#else

        // Initial set up and swapchain
        GLFWwindow                              *window                     = nullptr;

#endif
	
        // Application info
        AppInfo 				appInfo;
	
	// Vulkan objects
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


        // Descriptor sets and pipeline
        vk::raii::DescriptorSetLayout           descriptorSetLayout         = nullptr;
        vk::raii::PipelineLayout                pipelineLayout              = nullptr;
        vk::raii::Pipeline                      graphicsPipeline            = nullptr;
	
        // Depth management
        vk::raii::Image                         depthImage                  = nullptr;
        vk::raii::DeviceMemory                  depthImageMemory            = nullptr;
        vk::raii::ImageView                     depthImageView              = nullptr;
	
        // Mipmapping
        vk::raii::Image                         textureImage                = nullptr;
        vk::raii::DeviceMemory                  textureImageMemory          = nullptr;
        vk::raii::ImageView                     textureImageView            = nullptr;
        vk::raii::Sampler                       textureSampler              = nullptr;
	vk::Format				textureImageFormat	    = vk::Format::eUndefined;
	
	// Model data
        std::vector<Vertex>                     vertices;
        std::vector<uint32_t>                   indices;
	
	// Command pool - maybe???
        vk::raii::Buffer                        vertexBuffer                = nullptr;
        vk::raii::DeviceMemory                  vertexBufferMemory          = nullptr;
        vk::raii::Buffer                        indexBuffer                 = nullptr;
        vk::raii::DeviceMemory                  indexBufferMemory           = nullptr;

        // Uniform buffers
        std::vector<vk::raii::Buffer>           uniformBuffers;
        std::vector<vk::raii::DeviceMemory>     uniformBuffersMemory;
	std::vector<void *>			uniformBuffersMapped;

        // Descriptor pool
        vk::raii::DescriptorPool                descriptorPool              = nullptr;
        std::vector<vk::raii::DescriptorSet>    descriptorSets;
	
        // Command pool
        vk::raii::CommandPool                   commandPool                 = nullptr;
        std::vector<vk::raii::CommandBuffer>    commandBuffers;
	
        // Synchronization objects - Semaphores and fences
	std::vector<vk::raii::Semaphore>	    presentCompleteSemaphores;
        std::vector<vk::raii::Semaphore>        renderFinishedSemaphores;
        std::vector<vk::raii::Fence>            inFlightFences;
        uint32_t                                frameIndex                  = 0;
	
        bool 					                framebufferResized          = false;

        std::vector<const char *>         requiredDeviceExtensions    =
	{
		vk::KHRSwapchainExtensionName,
		vk::KHRCreateRenderpass2ExtensionName
	};
        
#if PLATFORM_DESKTOP

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
            , int width
            , int height
        )
        {
            auto app                                        = static_cast<VulkanApplication *>(glfwGetWindowUserPointer(window));
            app->framebufferResized                         = true;
        }
#endif

	public:


//******************************************************************************************
// 
//  Name:           initVulkan
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//                  createInstance();
//                  setupDebugMessenger();
//                  createSurface();
//                  pickPhysicalDevice();
//                  createLogicalDevice();
//                  createSwapChain();
//                  createImageViews();
//                  createDescriptorSetLayout();
//                  createGraphicsPipeline();
//                  createCommandPool();
//                  createDepthResources();
//                  createTextureImage();
//                  createTextureImageView();
//                  createTextureSampler();
//                  loadModel();
//                  createVertexBuffer();
//                  createIndexBuffer();
//                  createUniformBuffers();
//                  createDescriptorPool();
//                  createDescriptorSets();
//                  createCommandBuffers();
//                  createSyncObjects();
//
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
            createDescriptorSetLayout();
            createGraphicsPipeline();
            createCommandPool();
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

	private:
#if PLATFORM_DESKTOP
	
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
#endif


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
        
#if PLATFORM_DESKTOP

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

        void cleanup() const
        {
		glfwDestroyWindow(window);
		glfwTerminate();
        }
#endif
	

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
#if PLATFORM_DESKTOP
	    // On desktop, wait until the framebuffer has a non-zero size (e.g., when window is minimized)
            int   width         = 0
                , height        = 0;
            
                glfwGetFramebufferSize(
                      window
                    , &width
                    , &height
                );

                while (width == 0 || height == 0)
                {
                    glfwGetFramebufferSize(
                          window
                        , &width
                        , &height
                    );
                    glfwWaitEvents();
                }
#endif
	    // Wait for device to finishe operations
            device.waitIdle();

	    // Clean up old swap chain
            cleanupSwapChain();
	
	    // Create new swap chain and dependent resources
            createSwapChain();
            createImageViews();
	    createDepthResources();
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
                  .pApplicationName                         = "Hello Triangle"
                , .applicationVersion                       = VK_MAKE_VERSION(1, 0, 0)
                , .pEngineName                              = "No Engine"
                , .engineVersion                            = VK_MAKE_VERSION(1, 0, 0)
                , .apiVersion                               = VK_API_VERSION_1_3
            };
	    
            // Get required extensions.
            auto       extensions      = getRequiredInstanceExtensions();
            
	        // Create Instance
            vk::InstanceCreateInfo createInfo
            {
                  .pApplicationInfo                         = &appInfo
                , .enabledExtensionCount                    = static_cast<uint32_t>(extensions.size())
                , .ppEnabledExtensionNames                  = extensions.data()
            };
            
            instance = vk::raii::Instance(context, createInfo);
	    LOGI("Vulkan instance created");
        }


//******************************************************************************************
// 
//  Name:           setupDebugMessenger
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

	void setupDebugMessenger()
	{
		// Debug messenger setup is disabled for now to avoid compatibility issues
		// This is a simplified approach to get the code compiling
		if (!enableValidationLayers)
			return;

		LOGI("Debug messenger setup skipped for compatibility");
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
#if PLATFORM_DESKTOP
            VkSurfaceKHR _surface;

            // Create desktop surface using GLFW

            if (glfwCreateWindowSurface(  
                *instance
                , window
                , nullptr
                , &_surface
                ) != VK_SUCCESS
            )
            {
                throw std::runtime_error("Failed to create window surface!");
            }
            surface = vk::raii::SurfaceKHR(instance, _surface);
#else

            VkSurfaceKHR _surface;

            // Create Android surface
            VkAndroidSurfaceCreateInfoKHR 		createInfo
            {
                  .sType						            = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR
                , .window					                = androidAppState.nativeWindow
            };

            if (vkCreateAndroidSurfaceKHR(
                                                                                *instance
                                                                                , &createInfo
                                                                                , nullptr
                                                                                , &_surface
                                                                                ) != VK_SUCCESS
									)
            {
                throw std::runtime_error("Failed to create Android surface");
            }
            
            surface = vk::raii::SurfaceKHR(instance, _surface);
#endif
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
            auto                        queueFamilies       = physicalDevice.getQueueFamilyProperties();
            bool                        supportsGraphics    = std::ranges::any_of(  queueFamilies
                                                                                , [](auto const &qfp)
                                                                                {
                                                                                    return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics);
                                                                                });

            // Check if all required physicalDevice extensions are available
            auto            availableDeviceExtensions       = physicalDevice.enumerateDeviceExtensionProperties();
            bool            supportsAllRequiredExtensions   = 
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
	    
	    auto 			features			= physicalDevice
		.template getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
	    bool 			supportsRequiredFeatures	= features.template get<vk::PhysicalDevicevulkan13Features>().dynamicRendering &&
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
            physicalDevice                                          = *devIter;
	    
	    // Check for Vulkan profile support
	    VpProfileProperties			profileProperties;
#if PLATFORM_ANDROID
	    strcpy(profileProperties.name
			, VP_KHR_ROADMAP_2022_NAME);
#else	    
	    strcpy(profileProperties.profileName
			, VP_KHR_ROADMAP_2022_NAME);
#endif
	    profileProperties.specVersion			= VP_KHR_ROADMAP_2022_SPEC_VERSION;
	
	    VkBool32	            supported	            = VK_FALSE;
	    bool		    result			= false;
			
#if PLATFORM_ANDROID
			// Create a vp::ProfileDesc from our VpProfileProperties
			vp::ProfileDesc			profileDesc		        = 
			{
				  profileProperties.name
				, profileProperties.specVersion
			};
			
			// Use vp::GetProfileSupport for Android
			result			        = vp::GetProfileSupport(
				  *physicalDevice			// Pass the physical device directly
				, &profileDesc			    // Pass the profile description
				, &supported			    // Output parameter for support status
			);
#else
			// Use vpGetPhysicalDeviceProfileSupport for Desktop
			VkResult	            vk_result		  	    = vpGetPhysicalDeviceProfileSupport(
                                                                                              *instance
                                                                                            , *physicalDevice
                                                                                            , &profileProperties
                                                                                            , &supported
                                                                                            );
											    
			result				    = vk_result == static_cast<int>(vk::Result::eSuccess);
#endif
			const char *name 	= nullptr;
#ifdef PLATFORM_ANDROID
			name			= profileProperties.name;
#else
			name			= profileProperties.profileName;
#endif

			if (result && supported == VK_TRUE)
			{
				appInfo.profileSupported	                = true;
				appInfo.profile					= profileProperties;
				LOGI("Device supports Vulkan profile: %s", name);
			}
			else
			{
				LOGI("Device does not support Vulkan profile: %s", name);
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

		// Query for Vulkan 1.3 features
		auto							features 		= physicalDevice.getFeatures2();
		vk::PhysicalDeviceVulkan13Features			vulkan13Features;
		vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT	extendedDynamicStateFeatures;
		vulkan13Features.dynamicRendering						= vk::True;
		vulkan13Features.synchronization2						= vk::True;
		extendedDynamicStateFeatures.extendedDynamicState 				= vk::True;
		vulkan13Features.pNext								= &extendedDynamicStateFeatures;
		features.pNext									= &vulkan13Features;
		
		// Create a Device
		
            float                       queuePriority       = 0.5f;
            vk::DeviceQueueCreateInfo   deviceQueueCreateInfo 
            {
                  .queueFamilyIndex                         = queueIndex
                , .queueCount                               = 1
                , .pQueuePriorities                         = &queuePriority
            };
                // Create a vk::DeviceCreateInfo with the required features
                vk::DeviceCreateInfo                        deviceCreateInfo 
                {
                      .pNext                                = &features
                    , .queueCreateInfoCount                 = 1
                    , .pQueueCreateInfos                    = &deviceQueueCreateInfo
                    , .enabledExtensionCount                = static_cast<uint32_t>(requiredDeviceExtensions.size())
                    , .ppEnabledExtensionNames              = requiredDeviceExtensions.data()
                };
                
                // Create the device with the appropriate features
                device 			                            = vk::raii::Device(  physicalDevice
                                                                               , deviceCreateInfo);
									       
		queue		= vk::raii::Queue(device, queueIndex, 0);
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

            swapChain                                               = vk::raii::SwapchainKHR(device, swapChainCreateInfo);

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
            
                vk::ImageViewCreateInfo		imageViewCreateInfo
                {
                    , .viewType				                = vk::ImageViewType::e2D
                    , .format				                = swapChainSurfaceFormat.format
                    , .subresourceRange			            	= 
                    {
                        vk::ImageAspectFlagBits::eColor
                        , 0
                        , 1
                        , 0
                        , 1
                    }
                };
                
            for (auto &image : swapChainImages)
	    {
		imageViewCreateInfo.image		= image;
		swapChainImageViews.emplace_back(device, imageViewCreateInfo);
	    }
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
	    std::array bindings			= 
	    {
		vk::DescriptorSetLayoutBinding
		(
			0
			, vk::DescriptorType::eUniformBuffer
			, 1
			, vk::ShaderStageFlagBits::eVertex
			, nullptr
		)
		, vk::DescriptorSetLayoutBinding
		(
			1
			, vk::DescriptorType::eCombinedImageSampler
			, 1
			, vk::ShaderStageFlagBits::eFragment
			, nullptr
		)
            };
	    
            vk::DescriptorSetLayoutCreateInfo   layoutInfo
            {
                  .bindingCount                             = static_cast<uint32_t>(bindings.size())
                , .pBindings                                = bindings.data()
            };

            descriptorSetLayout                             = vk::raii::DescriptorSetLayout(device, layoutInfo);
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
		vk::raii::ShaderModule		shaderModule		= createShaderModule(this->readFile("shaders/slang.spv"));
		
		vk::PipelineShaderStageCreateInfo		vertShaderStageInfo
		{
			.stage						= vk::ShaderStageFlagBits::eVertex
			, .module					= *shaderModule
			, .pName					= "vertMain"
		};
		
		vk::PipelineShaderStageCreateInfo	fragShaderStageInfo
		{
			.stage						= vk::ShaderStageFlagBits::eFragment
			, .module					= *shaderModule
			, .pName					= "fragMain"
		};

            // Create shader stages
            vk::PipelineShaderStageCreateInfo                       shaderStages[] = 
            {
		vertShaderStageInfo
		, fragShaderStageInfo
            };

            // Vertex input
            auto                            bindingDescription      = Vertex::getBindingDescription();
            auto                            attributeDescriptions   = Vertex::getAttributeDescriptions();

            vk::PipelineVertexInputStateCreateInfo                  vertexInputInfo
            {
                  .vertexBindingDescriptionCount                    = 1
                , .pVertexBindingDescriptions                       = &bindingDescription
                , .vertexAttributeDescriptionCount                  = static_cast<uint32_t>(attributeDescriptions.size())
                , .pVertexAttributeDescriptions                     = attributeDescriptions.data()
            };

		    // Input assembly
            vk::PipelineInputAssemblyStateCreateInfo                inputAssembly
            {
                  .topology                                         = vk::PrimitiveTopology::eTriangleList
                , .primitiveRestartEnable                           = vk::False
            };

		    // Viewport and scissor
            vk::PipelineViewportStateCreateInfo                     viewportState
            {
                  .viewportCount                                    = 1
                , .scissorCount                                     = 1
            };

		    // Rasterization
            vk::PipelineRasterizationStateCreateInfo                rasterizer
            {
                  .depthClampEnable                                 = vk::False
                , .rasterizerDiscardEnable                          = vk::False
                , .polygonMode                                      = vk::PolygonMode::eFill
                , .cullMode                                         = vk::CullModeFlagBits::eBack	// Re-enabled culling for better performance
                , .frontFace                                        = vk::FrontFace::eClockwise		// Keeping Clockwise for glTF
                , .depthBiasEnable                                  = vk::False
                , .lineWidth                                        = 1.0f
            };

		    // Multisampling
            vk::PipelineMultisampleStateCreateInfo                  multisampling
            {
                  .rasterizationSamples                             = vk::SampleCountFlagBits::e1
                , .sampleShadingEnable                              = vk::False
            };
	    
		    // Depth/Stencil
            vk::PipelineDepthStencilStateCreateInfo                 depthStencil
            {
                  .depthTestEnable                                  = vk::True
                , .depthWriteEnable                                 = vk::True
                , .depthCompareOp                                   = vk::CompareOp::eLess
		, .depthBoundsTestEnable			    = vk::False
		, .stencilTestEnable				    = vk::False
            };

		    // Color blending
            vk::PipelineColorBlendAttachmentState                   colorBlendAttachment
            {
                  .blendEnable                                      = vk::False
                , .colorWriteMask                                   =       vk::ColorComponentFlagBits::eR
                                                                        |   vk::ColorComponentFlagBits::eG
                                                                        |   vk::ColorComponentFlagBits::eB
                                                                        |   vk::ColorComponentFlagBits::eA
            };

            vk::PipelineColorBlendStateCreateInfo                   colorBlending
            {
                  .logicOpEnable                                    = vk::False
                , .logicOp                                          = vk::LogicOp::eCopy
                , .attachmentCount                                  = 1
                , .pAttachments                                     = &colorBlendAttachment
            };

		    // Dynamic states
            std::vector   dynamicStates           = 
            {
                  vk::DynamicState::eViewport
                , vk::DynamicState::eScissor
            };
            
            vk::PipelineDynamicStateCreateInfo                      dynamicState
            {
                  .dynamicStateCount                                = static_cast<uint32_t>(dynamicStates.size())
                , .pDynamicStates                                   = dynamicStates.data()
            };

		    // Pipeline layout
            vk::PipelineLayoutCreateInfo                            pipelineLayoutInfo
            {
                  .setLayoutCount                                   = 1
                , .pSetLayouts                                      = &*descriptorSetLayout
		, .pushConstantRangeCount			    = 0
            };

            pipelineLayout                                          = vk::raii::PipelineLayout(device, pipelineLayoutInfo);

	    vk::Format			depthFormat		    = findDepthFormat();
	    vk::PipelineRenderingCreateInfo	pipelineRenderingCreateInfo
	    {
		.colorAttachmentCount				    = 1
		, .pColorAttachmentFormats			    = &swapChainSurfaceFormat.format
		, .depthAttachmentFormat			    = depthFormat
	    };
	    
            // Create the graphics pipeline
            vk::GraphicsPipelineCreateInfo		pipelineInfo
            {
		  .pNext					    = &pipelineRenderingCreateInfo
		, .stageCount                                       = 2
                , .pStages                                          = shaderStages
                , .pVertexInputState                                = &vertexInputInfo
                , .pInputAssemblyState                              = &inputAssembly
                , .pViewportState                                   = &viewportState
                , .pRasterizationState                              = &rasterizer
                , .pMultisampleState                                = &multisampling
                , .pDepthStencilState                               = &depthStencil
                , .pColorBlendState                                 = &colorBlending
                , .pDynamicState                                    = &dynamicState
                , .layout                                           = *pipelineLayout
                , .renderPass                                       = nullptr
            };
            
	        // Create the pipeline
            graphicsPipeline = vk::raii::Pipeline(device, nullptr, pipelineInfo);
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

            commandPool = vk::raii::CommandPool(device, poolInfo);
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
            vk::Format 			depthFormat                                     = findDepthFormat();

            createImage(
                  swapChainExtent.width
                , swapChainExtent.height
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
        ) const
        {
            for (const auto format : candidates)
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
//  Returns:        [[nodiscard]] vk::Format
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

	[[nodiscard]] vk::Format findDepthFormat() const
	{
		return findSupportedFormat(
			{
				vk::Format::eD32Sfloat
				, vk::Format::eD32SfloatS8Uint
				, vk::Format::eD24UnormS8Uint
			}
			, vk::ImageTiling::eOptimal
			, vk::FormatFeatureFlagBits::eDepthStencilAttachment
		);
	}
	
	static bool hasStencilComponent(vk::Format format)
	{
		return format == vk::Format::eD32SfloatS8Uint || format == vk::Format::eD24UnormS8Uint;
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
		// Load KTX2 texture instead of using stb_image
		ktxTexture	*kTexture;
		KTX_error_code	result		= 	ktxTexture_CreateFromNamedFile(
									TEXTURE_PATH.c_str()
									, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT
									, &kTexture
							);

            if (result != KTX_SUCCESS)
            {
                throw std::runtime_error("Failed to load ktx texture image!");
            }

	    // Get texture dimensions and data
	    uint32_t			texWidth			= kTexture->baseWidth;
	    uint32_t			texHeight			= kTexture->baseHeight;
	    ktx_size_t			imageSize			= ktxTexture_GetImageSize(kTexture, 0);
	    ktx_uint8_t			*ktxTextureData			= ktxTexture_GetData(kTexture);
	    

		    // Create staging buffer
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

		    // Copy pixel data to staging buffer
            void                        *data = stagingBufferMemory.mapMemory(0, imageSize);

            memcpy(  
                  data
                , ktxTextureData
                , imageSize
            );

            stagingBufferMemory.unmapMemory();

            // Determine the Vulkan format from KTX format
	    vk::Format		textureFormat;
	    
            if (kTexture->classId == ktxTexture2_c)
            {
		// For KTX2 files, we can get the format directly
		auto *ktx2						= reinterpret_cast<ktxTexture2 *>(kTexture);
		textureFormat						= static_cast<vk::Format>(ktx2->vkFormat);
		if (textureFormat == vk::Format::eUndefined)
		{
			// If the format is undefined, fall backto a reasonable default
			textureFormat					= vk::Format::eR8G8B8A8Unorm;
		}
            }
	    else
	    {
		// For KTX1 files or if we can't determine the format, use a reasonable default
		textureFormat						= vk::Format::eR8G8B8A8Unorm;
	    }
	    
	    textureImageFormat						= textureFormat;
	    
            // Create image
            createImage(  
                  texWidth
                , texHeight
                , textureFormat
                , vk::ImageTiling::eOptimal
                , vk::ImageUsageFlagBits::eTransferDst
                | vk::ImageUsageFlagBits::eSampled
                , vk::MemoryPropertyFlagBits::eDeviceLocal
                , textureImage
                , textureImageMemory
            );

            transitionImageLayout(  
                  textureImage
                , vk::ImageLayout::eUndefined
                , vk::ImageLayout::eTransferDstOptimal
            );

            copyBufferToImage(
                  stagingBuffer
                , textureImage
                , texWidth
                , texHeight
            );
            
            transitionImageLayout(
                  textureImage
                , vk::ImageLayout::eTransferDstOptimal
                , vk::ImageLayout::eShaderReadOnlyOptimal
            );
	    
	    ktxTexture_Destroy(kTexture);
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
            textureImageView                                = createImageView(  
                                                                                textureImage
                                                                              , textureImageFormat
                                                                              , vk::ImageAspectFlagBits::eColor
                                                                              , 1
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
            vk::SamplerCreateInfo               samplerInfo
            {
                  .magFilter                                = vk::Filter::eLinear
                , .minFilter                                = vk::Filter::eLinear
                , .mipmapMode                               = vk::SamplerMipmapMode::eLinear
                , .addressModeU                             = vk::SamplerAddressMode::eRepeat
                , .addressModeV                             = vk::SamplerAddressMode::eRepeat
                , .addressModeW                             = vk::SamplerAddressMode::eRepeat
                , .anisotropyEnable                         = VK_TRUE
                , .maxAnisotropy                            = 16.0f
                , .compareEnable                            = VK_FALSE
                , .compareOp                                = vk::CompareOp::eAlways
                , .borderColor                              = vk::BorderColor::eIntOpaqueBlack
                , .unnormalizedCoordinates                  = VK_FALSE
            };

            textureSampler                                  = device.createSampler(samplerInfo);
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

#if PLATFORM_ANDROID
            // Load OBJ file from Android assets
            std::optional<AssetManagerType *>	optionalAssetManager	= assetManager;
            std::vector<char>			        objData	                = readFile(MODEL_PATH, optionalAssetManager);
            std::string				            objString(objData.begin(), objData.end());
            std::istringstream			        objStream(objString);
            
            if (!tinyobj::LoadObj(
                    &attrib
                    , &shapes
                    , &materials
                    , &warn
                    , &err
                    , &objStream
                    )
                )
            {
                throw std::runtime_error("Failed to load model: " + MODEL_PATH + " - " + warn + err);
            }
#else
		    // Load OBJ file from filesystem
            if (!tinyobj::LoadObj(
                           &attrib
                         , &shapes
                         , &materials
                         , &warn
                         , &err
                         , MODEL_PATH.c_str()
                        )
                )
            {
                throw std::runtime_error("Failed to load model: " + MODEL_PATH + " - " + warn + err);
            }
#endif
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

                    if (uniqueVertices.count(vertex) == 0)
                    {
                        uniqueVertices[vertex]              = static_cast<uint32_t>(vertices.size());
                        vertices.push_back(vertex);
                    }

                    indices.push_back(uniqueVertices[vertex]);
                }
            }
	    
	        LOG_INFO("Model loaded successfully");
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
            
            vk::raii::Buffer        stagingBuffer           = nullptr;
            vk::raii::DeviceMemory  stagingBufferMemory     = nullptr;

            createBuffer(
                  bufferSize
                , vk::BufferUsageFlagBits::eTransferSrc
                , vk::MemoryPropertyFlagBits::eHostVisible
                | vk::MemoryPropertyFlagBits::eHostCoherent
                , stagingBuffer
                , stagingBufferMemory
            );

            void *data;
	        data                                      		= stagingBufferMemory.mapMemory(0, bufferSize);

            memcpy(
                  data
                , vertices.data()
                , (size_t) bufferSize
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

            vk::raii::Buffer        stagingBuffer           = nullptr;
            vk::raii::DeviceMemory  stagingBufferMemory     = nullptr;

            createBuffer(
                  bufferSize
                , vk::BufferUsageFlagBits::eTransferSrc
                , vk::MemoryPropertyFlagBits::eHostVisible
                | vk::MemoryPropertyFlagBits::eHostCoherent
                , stagingBuffer
                , stagingBufferMemory
            );

            void                    *data;
	        data			                                = stagingBufferMemory.mapMemory(0, bufferSize);

            memcpy(
                  data
                , indices.data()
                , (size_t) bufferSize
            );

            stagingBufferMemory.unmapMemory();

            createBuffer(  
                  bufferSize
                , vk::BufferUsageFlagBits::eTransferDst
                | vk::BufferUsageFlagBits::eIndexBuffer
                , vk::MemoryPropertyFlagBits::eDeviceLocal
                , indexBuffer
                , indexBufferMemory
            );

            copyBuffer(
                  stagingBuffer
                , indexBuffer
                , bufferSize
            );
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
            vk::DeviceSize              bufferSize          = sizeof(UniformBufferObject);

            uniformBuffers.clear();
            uniformBuffersMemory.clear();
	    
            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            {
                uniformBuffers.push_back(nullptr);
                uniformBuffersMemory.push_back(nullptr);
            }

            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            {
                createBuffer(
                      bufferSize
                    , vk::BufferUsageFlagBits::eUniformBuffer
                    , vk::MemoryPropertyFlagBits::eHostVisible
                    | vk::MemoryPropertyFlagBits::eHostCoherent
                    , uniformBuffers[i]
                    , uniformBuffersMemory[i]
                );
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
            std::array <vk::DescriptorPoolSize, 2> poolSizes =
            {
                vk::DescriptorPoolSize
                {
                      .type						            = vk::DescriptorType::eUniformBuffer
                    , .descriptorCount				        = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT)
                }
                , vk::DescriptorPoolSize
                {
                      .type						            = vk::DescriptorType::eCombinedImageSampler
                    , .descriptorCount				        = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT)
                }
            };

            vk::DescriptorPoolCreateInfo    poolInfo
            {
                  .maxSets                                  = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT)
                , .poolSizeCount                            = static_cast<uint32_t>(poolSizes.size())
                , .pPoolSizes                               = poolSizes.data()
            };

            descriptorPool                                  = device.createDescriptorPool(poolInfo);
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
                                                            , *descriptorSetLayout);
            vk::DescriptorSetAllocateInfo           allocInfo
            {
                  .descriptorPool                           = *descriptorPool
                , .descriptorSetCount                       = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT)
                , .pSetLayouts                              = layouts.data()
            };

            descriptorSets                                  = device.allocateDescriptorSets(allocInfo);

            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            {
                vk::DescriptorBufferInfo        bufferInfo
                {
                      .buffer                               = *uniformBuffers[i]
                    , .offset                               = 0
                    , .range                                = sizeof(UniformBufferObject)
                };

                vk::DescriptorImageInfo         imageInfo
                {
                      .sampler                              = *textureSampler
                    , .imageView                            = *textureImageView
                    , .imageLayout                          = vk::ImageLayout::eShaderReadOnlyOptimal
                };

                std::array<vk::WriteDescriptorSet, 2> descriptorWrites	=
                {
                    vk::WriteDescriptorSet
                    {
                          .dstSet                           = *descriptorSets[i]
                        , .dstBinding                       = 0
                        , .dstArrayElement                  = 0
                        , .descriptorCount                  = 1
                        , .descriptorType                   = vk::DescriptorType::eUniformBuffer
                        , .pBufferInfo                      = &bufferInfo
                    }
                    , vk::WriteDescriptorSet
                    {
                          .dstSet                           = *descriptorSets[i]
                        , .dstBinding                       = 1
                        , .dstArrayElement                  = 0
                        , .descriptorCount                  = 1
                        , .descriptorType                   = vk::DescriptorType::eCombinedImageSampler
                        , .pImageInfo                       = &imageInfo
                    }
                };

                device.updateDescriptorSets(  descriptorWrites
                                            , nullptr);
            }
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
            commandBuffers.reserve(MAX_FRAMES_IN_FLIGHT);
	    
            vk::CommandBufferAllocateInfo allocInfo
            {
                  .commandPool                              = *commandPool
                , .level                                    = vk::CommandBufferLevel::ePrimary
                , .commandBufferCount                       = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT)
            };
	    
            commandBuffers                                  = device.allocateCommandBuffers(allocInfo);
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
            imageAvailableSemaphores.reserve(MAX_FRAMES_IN_FLIGHT);
            renderFinishedSemaphores.reserve(swapChainImages.size());
            inFlightFences.reserve(MAX_FRAMES_IN_FLIGHT);
            
            vk::SemaphoreCreateInfo		    semaphoreInfo{};
            vk::FenceCreateInfo		        fenceInfo
                                            {
                                                .flags		= vk::FenceCreateFlagBits::eSignaled
                                            };
		
            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            {
		        imageAvailableSemaphores.push_back(device.createSemaphore(semaphoreInfo));
                inFlightFences.push_back(device.createFence(fenceInfo));
            }

            for (size_t i = 0; i < swapChainImages.size(); i++)
            {
                renderFinishedSemaphores.push_back(device.createSemaphore(semaphoreInfo));
            }
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

        void recordCommandBuffer(
            vk::raii::CommandBuffer &commandBuffer
            , uint32_t imageIndex
        )
        {
		    vk::CommandBufferBeginInfo	beginInfo{};
            commandBuffer.begin(beginInfo);

            vk::ClearValue clearValues[]
            {
                vk::ClearValue
                {
                    vk::ClearColorValue(  
                          0.0f
                        , 0.0f
                        , 0.0f
                        , 1.0f
                    )
                }
                , vk::ClearValue 
                {
                    vk::ClearDepthStencilValue(1.0f, 0)
                }
            };

            vk::RenderPassBeginInfo		renderPassInfo
            {
                  .renderPass			                    = *renderPass
                , .framebuffer			                    = *swapChainFramebuffers[imageIndex]
                , .renderArea			                    = 
                    {
                          .offset					        = {0,0}
                        , .extent					        = swapChainExtent
                    }
                , .clearValueCount		                    = 2
                , .pClearValues			                    = clearValues
            };
            
            commandBuffer.beginRenderPass(
                  renderPassInfo
                , vk::SubpassContents::eInline
            );

            commandBuffer.bindPipeline(  
                  vk::PipelineBindPoint::eGraphics
                , *graphicsPipeline
            );

            vk::Viewport 		        viewport
            {
                  .x			                            = 0.0f
                , .y			                            = 0.0f
                , .width		                            = static_cast<float>(swapChainExtent.width)
                , .height		                            = static_cast<float>(swapChainExtent.height)
                , .minDepth		                            = 0.0f
                , .maxDepth		                            = 1.0f
            };
            
            commandBuffer.setViewport(0, viewport);
            
            vk::Rect2D scissor
            {
                  .offset		                            = {0, 0}
                , .extent	                                = swapChainExtent
            };

            commandBuffer.setScissor(0, scissor);

            commandBuffer.bindVertexBuffers(  
                  0
                , {*vertexBuffer}
                , {0}
            );

            commandBuffer.bindIndexBuffer(  
                  *indexBuffer
                , 0
                , vk::IndexType::eUint32
            );

            commandBuffer.bindDescriptorSets(
                  vk::PipelineBindPoint::eGraphics
                , *pipelineLayout
                , 0
                , *descriptorSets[frameIndex]
                , nullptr
            );

            commandBuffer.drawIndexed(
                  static_cast<uint32_t>(indices.size())
                , 1
                , 0
                , 0
                , 0
            );

	        commandBuffer.endRenderPass();
	    
            commandBuffer.end();
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
            static_cast<void>(device.waitForFences(
                  {*inFlightFences[frameIndex]}
                , VK_TRUE
                , UINT64_MAX)
			);

            auto [
                  result
                , imageIndex
                ]           = swapChain.acquireNextImage(
			                                          UINT64_MAX
                                                    , *imageAvailableSemaphores[frameIndex]
                                                    , nullptr
                                                    );

            // Due to VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS being defined, eErrorOutOfDateKHR can be checked as a result
            // here and does not need to be caught by an exception.

            if (result == vk::Result::eErrorOutOfDateKHR)
            {
                recreateSwapChain();
                return;
            }

            // On other success codes than eSuccess and eSuboptimalKHR we just throw an exception.
            // On any error code, aquireNextImage already threw an exception.

            if (   result != vk::Result::eSuccess 
                && result != vk::Result::eSuboptimalKHR)
            {
                assert(   result == vk::Result::eTimeout 
                       || result == vk::Result::eNotReady);
                throw std::runtime_error("Failed to acquire swap chain image!");
            }

		    // Update uniform buffer with current transformation
            updateUniformBuffer(frameIndex);

            // Only reset the fence if we are submitting work                    
            device.resetFences(*inFlightFences[frameIndex]);

            commandBuffers[frameIndex].reset();
            recordCommandBuffer(
                  commandBuffers[frameIndex]
                , imageIndex
            );

            vk::PipelineStageFlags  waitDestinationStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);

            const vk::SubmitInfo        submitInfo
            {
                  .waitSemaphoreCount                       = 1
                , .pWaitSemaphores                          = &*imageAvailableSemaphores[frameIndex]
                , .pWaitDstStageMask                        = &waitDestinationStageMask
                , .commandBufferCount                       = 1
                , .pCommandBuffers                          = &*commandBuffers[frameIndex]
                , .signalSemaphoreCount                     = 1
                , .pSignalSemaphores                        = &*renderFinishedSemaphores[imageIndex]
            };

            queue.submit(
                  submitInfo
                , *inFlightFences[frameIndex]
            );

            const vk::PresentInfoKHR    presentInfoKHR
            {
                  .waitSemaphoreCount                       = 1
                , .pWaitSemaphores                          = &*renderFinishedSemaphores[imageIndex]
                , .swapchainCount                           = 1
                , .pSwapchains                              = &*swapChain
                , .pImageIndices                            = &imageIndex
            };

            result                                          = queue.presentKHR(presentInfoKHR);

            // Due to VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS being defined, eErrorOutOfDateKHR can be checked as a result
            // here and does not need to be caught by an exception.

            if (   (result == vk::Result::eSuboptimalKHR) 
                || (result == vk::Result::eErrorOutOfDateKHR) 
                || framebufferResized)
            {
                framebufferResized                          = false;
                recreateSwapChain();
            }
            else
            {
                // There are no other success codes than eSuccess; on any error code, presentKHR already threw an exception.
                assert(result == vk::Result::eSuccess);
            }

            frameIndex                                      = (frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
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
        
	    // Get required extensions
        std::vector<const char *> getRequiredInstanceExtensions()
        {
#if PLATFORM_ANDROID
            // Android requires these extensions
            std::vector<const char *>  extensions		    = 
            {
                  VK_KHR_SURFACE_EXTENSION_NAME
                , VK_KHR_ANDROID_SURFACE_EXTENSION_NAME
            };
#else
            // Get the required extensions from GLFW
            uint32_t			glfwExtensionCount		    = 0;
            auto				glfwExtensions			    = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
            std::vector<const char *> 	extensions(
                                              glfwExtensions
                                            , glfwExtensions + glfwExtensionCount
                                        );
#endif
            // Check if the debug utils extension is available
            std::vector<vk::ExtensionProperties>	props	= context.enumerateInstanceExtensionProperties();
            bool				debugUtilsAvailable	        = std::ranges::any_of(
                                                                            props
                                                                            , [](vk::ExtensionProperties const &ep
                                                                            )
                                                                            {
                                                                                return strcmp(
                                                                                        ep.extensionName
                                                                                        , vk::EXTDebugUtilsExtensionName
                                                                                        ) == 0;
                                                                            });
            // Always include the debug utils extension if available
            if (debugUtilsAvailable)
            {
                extensions.push_back(vk::EXTDebugUtilsExtensionName);
#if PLATFORM_DESKTOP
            }
            else
            {
                LOG_INFO("VK_EXT_debug_utils extension not available.  Validation layers may not work.");
#endif
		    }
		
		return extensions;
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

        vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR> &availableFormats)
        {
            assert(!availableFormats.empty());
            const auto formatIt                             = std::ranges::find_if(  
                                                                              av<ailableFormats
                                                                            , [](const auto &format) 
                                                                            {
                                                                                    return format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
                                                                            }
                                                                            );
            return formatIt != availableFormats.end() ? *formatIt : availableFormats[0];
        }
        

//******************************************************************************************
// 
//  Name:           chooseSwapPresentMode
//  Arguments:      N/A
//  Returns:        static vk::SurfaceFormatKHR
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

	    // Choose swap present mode
        static vk::PresentModeKHR chooseSwapPresentMode(std::vector<vk::PresentModeKHR> const &availablePresentModes)
        {
            assert(std::ranges::any_of(
					                     availablePresentModes
                                       , [](auto presentMode)
                                    {
                                        return presentMode == vk::PresentModeKHR::eFifo;
                                    }
				)
			);

            return std::ranges::any_of(
					                     availablePresentModes
                                       , [](const vk::PresentModeKHR value)
                                    {
                                        return vk::PresentModeKHR::eMailbox == value;
                                    }
				) ?
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
            else
            {
#if PLATFORM_ANDROID
                // Get the window size from Android
                int32_t		            width			    = ANativeWindow_getWidth(androidApp->window);
                int32_t		            height			    = ANativeWindow_getHeight(androidApp->window);
#else
		        // Get the window size from GLFW
                int width, height;

                glfwGetFramebufferSize(
                      window
                    , &width
                    , &height
                );
#endif

                vk::Extent2D		    actualExtent		= 
                {
                      static_cast<uint32_t>(width)
                    , static_cast<uint32_t>(height)
                };

                actualExtent.width	                        = std::clamp(  actualExtent.width
                                                                         , capabilities.minImageExtent.width
                                                                         , capabilities.maxImageExtent.width);
                actualExtent.height	                        = std::clamp(  actualExtent.height
                                                                         , capabilities.minImageExtent.height
                                                                         , capabilities.maxImageExtent.height);
				       
		        return actualExtent;
		    }
        }


//******************************************************************************************
// 
//  Name:           querySwapChainSupport
//  Arguments:      filename
//  Returns:        SwapChainSupportDetails
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        SwapChainSupportDetails querySwapChainSupport(vk::raii::PhysicalDevice device)
        {
            SwapChainSupportDetails		    details;
            details.capabilities				            = device.getSurfaceCapabilitiesKHR(*surface);
            details.formats					                = device.getSurfaceFormatsKHR(*surface);
            details.presentModes				            = device.getSurfacePresentModesKHR(*surface);
            
            return details;
        }
        

//******************************************************************************************
// 
//  Name:           createBuffer
//  Arguments:      N/A
//  Returns:        void
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

            buffer                                          = device.createBuffer(bufferInfo);

            vk::MemoryRequirements  memRequirements         = buffer.getMemoryRequirements();

            vk::MemoryAllocateInfo  allocInfo
            {
                  .allocationSize                           = memRequirements.size
                , .memoryTypeIndex                          = findMemoryType(
                                                                               memRequirements.memoryTypeBits
                                                                             , properties
                                                                            )
            };

            bufferMemory                                    = device.allocateMemory(allocInfo);
            
            buffer.bindMemory(*bufferMemory, 0);
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
              vk::raii::Buffer              &srcBuffer
            , vk::raii::Buffer              &dstBuffer
            , vk::DeviceSize                size
        )
        {
            vk::CommandBufferAllocateInfo   allocInfo
            {
                  .commandPool					            = *commandPool
                , .level					                = vk::CommandBufferLevel::ePrimary
                , .commandBufferCount				        = 1
            };
            
            vk::raii::CommandBuffer		    commandBuffer	= std::move(device.allocateCommandBuffers(allocInfo)[0]);
            
            vk::CommandBufferBeginInfo		beginInfo
            {
                .flags						                = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
            };
            
            commandBuffer.begin(beginInfo);
	    
            vk::BufferCopy		            copyRegion
            {
                  .srcOffset					            = 0
                , .dstOffset					            = 0
                , .size	                                    = size
            };
	
            commandBuffer.copyBuffer(  
                  *srcBuffer
                , *dstBuffer
                , copyRegion
            );

            commandBuffer.end();
            
            vk::SubmitInfo 		            submitInfo
            {
                  .commandBufferCount		                = 1
                , .pCommandBuffers		                    = &*commandBuffer
            };
            
            queue.submit(submitInfo, nullptr);
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
            vk::ImageViewCreateInfo         viewInfo
            {
                  .image                                    = *image
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

            return vk::raii::ImageView(device, viewInfo);
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
                , .extent                                   = 
                { 
                      width
                    , height
                    , 1 
                }
                , .mipLevels                                = mipLevels
                , .arrayLayers                              = 1
                , .samples                                  = vk::SampleCountFlagBits::e1
                , .tiling                                   = tiling
                , .usage                                    = usage
                , .sharingMode                              = vk::SharingMode::eExclusive
                , .initialLayout                            = vk::ImageLayout::eUndefined
            };

            image                                           = vk::raii::Image(device, imageInfo);
            
            vk::MemoryRequirements          memRequirements = image.getMemoryRequirements();
	    
            vk::MemoryAllocateInfo          allocInfo
            {
                  .allocationSize                           = memRequirements.size
                , .memoryTypeIndex                          = findMemoryType(
                                                                               memRequirements.memoryTypeBits
                                                                             , properties
                                                                            )
            };
	    
            imageMemory                                     = vk::raii::DeviceMemory(device, allocInfo);
            image.bindMemory(*imageMemory, 0);
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
              vk::raii::Image         		&image
	        , vk::Format			        format
            , vk::ImageLayout               oldLayout
            , vk::ImageLayout               newLayout
        )
        {
            vk::CommandBufferAllocateInfo   allocInfo
            {
                  .commandPool					            = *commandPool
                , .level					                = vk::CommandBufferLevel::ePrimary
                , .commandBufferCount				        = 1
            };
		
            vk::raii::CommandBuffer         commandBuffer   = std::move(device.allocateCommandBuffers(allocInfo)[0]);

            vk::CommandBufferBeginInfo	    beginInfo
            {
                .flags					                    = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
            };
            
            commandBuffer.begin(beginInfo);

            vk::ImageMemoryBarrier          barrier         
            {
                  .oldLayout                                = oldLayout
                , .newLayout                                = newLayout
                , .srcQueueFamilyIndex				        = VK_QUEUE_FAMILY_IGNORED
                , .dstQueueFamilyIndex				        = VK_QUEUE_FAMILY_IGNORED
                , .image                                    = image
                , .subresourceRange                         = 
                {
                      .aspectMask				            = vk::ImageAspectFlagBits::eColor
                    , .baseMipLevel		                    = 0
                    , .levelCount		                    = 1
                    , .baseArrayLayer		                = 0
                    , .layerCount		                    = 1
                }
            };

            vk::PipelineStageFlags sourceStage;
            vk::PipelineStageFlags destinationStage;

            if (   oldLayout == vk::ImageLayout::eUndefined 
                && newLayout == vk::ImageLayout::eTransferDstOptimal)
            {
                barrier.srcAccessMask                       = vk::AccessFlagBits::eNone;
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

            commandBuffer.pipelineBarrier(
                  sourceStage
                , destinationStage
                , vk::DependencyFlagBits::eByRegion
                , nullptr
                , nullptr
                , barrier
            );

            commandBuffer.end();
	    
            vk::SubmitInfo			        submitInfo
            {
                  .commandBufferCount				        = 1
                , .pCommandBuffers				            = &*commandBuffer
            };
            
            queue.submit(submitInfo, nullptr);
            queue.waitIdle();
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
		      vk::raii::Buffer    	        &buffer
            , vk::raii::Image     	        &image
            , uint32_t      		        width
            , uint32_t      		        height
        )
        {
            vk::CommandBufferAllocateInfo	allocInfo
            {
                  .commandPool					            = *commandPool
                , .level					                = vk::CommandBufferLevel::ePrimary
                , .commandBufferCount				        = 1
            };

            vk::raii::CommandBuffer         commandBuffer   = std::move(device.allocateCommandBuffers(allocInfo)[0]);
            
            vk::CommandBufferBeginInfo		beginInfo
            {
                .flags						                = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
            };
            
            commandBuffer.begin(beginInfo);
	    
            vk::BufferImageCopy             region
            {
                  .bufferOffset                             = 0
                , .bufferRowLength                          = 0
                , .bufferImageHeight                        = 0
                , .imageSubresource                         =
                {
                      .aspectMask			                = vk::ImageAspectFlagBits::eColor
                    , .mipLevel				                = 0
                    , .baseArrayLayer			            = 0
                    , .layerCount			                = 1
                }
                , .imageOffset                              = { 0, 0, 0 }
                , .imageExtent                              = { width, height, 1}
            };

            commandBuffer.copyBufferToImage(
                  *buffer
                , *image
                , vk::ImageLayout::eTransferDstOptimal
                , region
            );

            commandBuffer.end();
	    
            vk::SubmitInfo submitInfo
            {
                  .commandBufferCount				        = 1
                , .pCommandBuffers				            = &*commandBuffer
            };
            
            queue.submit(submitInfo, nullptr);
            queue.waitIdle();
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

        void updateUniformBuffer(uint32_t currentImage)
        {
            static auto             startTime               = std::chrono::high_resolution_clock::now();

            auto                    currentTime             = std::chrono::high_resolution_clock::now();
            float                   time                    = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

            UniformBufferObject     ubo{};

            ubo.model                                       = glm::rotate(
                                                                          glm::mat4(1.0f)
                                                                        , time * glm::radians(90.0f)
                                                                        , glm::vec3(0.0f, 0.0f, 1.0f)
                                                                        );
            
            ubo.view                                        = glm::lookAt(
                                                                          glm::vec3(2.0f, 2.0f, 2.0f)
                                                                        , glm::vec3(0.0f, 0.0f, 0.0f)
                                                                        , glm::vec3(0.0f, 0.0f, 1.0f)
                                                                        );

            ubo.proj                                        = glm::perspective(
                                                                          glm::radians(45.0f)
                                                                        , swapChainExtent.width / (float) swapChainExtent.height
                                                                        , 0.1f
                                                                        , 10.0f
                                                                        );

            ubo.proj[1][1] *= -1;

		    void *data;
		    data						                    = uniformBuffersMemory[currentImage].mapMemory(0, sizeof(ubo));

            memcpy(
			      data
                , &ubo
                , sizeof(ubo)
            );
		
		    uniformBuffersMemory[currentImage].unmapMemory();
        }
	
#if PLATFORM_ANDROID
	// Handle app commands
	static void handleAppCommand(
          android_app *app
        , int32_t cmd
	)
	{
		auto 		                    *vulkanApp			= static_cast<HelloTriangleApplication *>(app->userData);
		switch (cmd)
		{
			case APP_CMD_INIT_WINDOW:
				// Window created, initialize Vulkan
				if (app->window != nullptr)
				{
					vulkanApp->initVulkan();
				}
				break;
			case APP_CMD_TERM_WINDOW:
				// Window destroyed, clean up Vulkan
				vulkanApp->cleanup();
				break;
			default:
				break;
		}
	}
	
	
//******************************************************************************************
// 
//  Name:           handleInputEvent
//  Arguments:      android_app *app
//		            AInputEvent *event
//  Returns:        int32_t
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************
	
	static int32_t handleInputEvent(
		  android_app *app
		, AInputEvent *event
	)
	{
		auto *vulkanApp					= static_cast<HelloTriangleApplication *>(app->userData);
		if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION)
		{
			// Handle touch events
			float x					= AMotionEvent_getX(event, 0);
			float y					= AMotionEvent_getY(event, 0);
			
			// Process touch coordinates
			LOGI("Touch at: %f, %f", x, y);
			
			return 1;
		}
		return 0;
	}
#endif
};
        
// Platform-specific entry point
#if PLATFORM_ANDROID
// Android main entry point
void android_main(android_app *app)
{
	// Make sure glue isn't stripped
	app_dummy();
	
	try
	{
		// Create and run the Vulkan application
		HelloTriangleApplication 	vulkanApp(app);
		vulkanApp.run();
	}
	catch (const std::exception &e)
	{
		LOGE("Exception caught: %s", e.what());
	}
}
#else

// Cross-platform file reading function
        

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

std::vector<char> readFile(const std::string &filename)
{
#if PLATFORM_ANDROID
	// Android asset loading
	if (androidAppState.app == nullptr)
	{
		LOGE("Android app not initialized");
		throw std::runtime_error("Android app not initialized");
	}
	AAsset *asset 			                            = AAssetManager_open(
                                                                      androidAppState.app->activity->assetManager
                                                                    , filename.c_str()
                                                                    , AASSET_MODE_BUFFER
                                                                    );
	if (!asset)
	{
		throw std::runtime_error("Failed to open file: " + filename);
	}

		size_t			size		= AAsset_getLength(asset);
		std::vector<char>	buffer(size);
		
		AAsset_read(  
			asset
			, buffer.data()
			, size
		);
		AAsset_close(asset);
		
#else
	    // Desktop version or Android fallback to filesystem
            std::ifstream file(  filename
                               , std::ios::ate | std::ios::binary);

            if (!file.is_open())
            {
                throw std::runtime_error("Failed to open file:" + filename);
            }
	    
	    size_t	fileSize		= static_cast<size_t>(file.tellg());
            std::vector<char> buffer(fileSize);
	    
            file.seekg(0);
            file.read(buffer.data(), fileSize);
            file.close();
#endif
            return buffer;
        }

// Desktop main entry point

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
#endif