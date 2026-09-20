#include <algorithm>
#include <array>
#include <assert.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#   include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#	define GLFW_INCLUDE_VULKAN        // REQUIRED only for GLFW CreateWindowSurface.
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

constexpr uint32_t                  WIDTH                   = 800;
constexpr uint32_t                  HEIGHT                  = 600;
constexpr uint32_t			PARTICLE_COUNT		= 8192;
constexpr int                       MAX_FRAMES_IN_FLIGHT    = 2;

struct UniformBufferObject
{
	float			deltaTime			= 1.0f;
};

struct Particle
{
    glm::vec2 position;
    glm::vec2 velocity;
    glm::vec4 color;

    static vk::VertexInputBindingDescription getBindingDescription()
    {
        return 
	{
              0
            , sizeof(Particle)
            , vk::VertexInputRate::eVertex
        };
    }

    static std::array<vk::VertexInputAttributeDescription, 2> getAttributeDescriptions()
    {
        return
        {
            vk::VertexInputAttributeDescription
            (    
                   0
                ,  0
                , vk::Format::eR32G32Sfloat
                , offsetof(  
			     Particle
                           , position
			   )
            )
            , vk::VertexInputAttributeDescription
            (
                  1
                , 0
                , vk::Format::eR32G32B32A32Sfloat
                , offsetof(  
			     Particle
                           , color
			   )
            ),
        };
    }
};

// Simple logging function
template <typename... Args>
void log(Args &&...args)
{
	// Only log in debug builds
#ifdef	_DEBUG
	(std::cout << ... << std::forward<Args>(args)) << std::endl;
#endif
}

class ThreadSafeResourceManager
{
	private:
		std::mutex					resourceMutex;
		std::vector<vk::raii::CommandPool>		commandPools;
		std::vector<vk::raii::CommandBuffer>		commandBuffers;
		
	public:
	

//******************************************************************************************
// 
//  Name:           createThreadCommandPools
//  Arguments:      vk::raii::Device &device
//		    uint32_t queueFamilyIndex
//		    uint32_t threadCount
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************
	
		void createThreadCommandPools(
			vk::raii::Device 			&device
			, uint32_t				queueFamilyIndex
			, uint32_t				threadCount
		)
		{
			std::lock_guard<std::mutex>		lock(resourceMutx);
			
			commandBuffers.clear();
			commandPools.clear();
			
			for (uint32_t i = 0; i < threadCount; i++)
			{
				vk::CommandPoolCreateInfo	poolInfo
				{
					.flags						= vk::CommandPoolCreateFlagBits::eResetCommandBuffer
					, .queueFamilyIndex				= queueFamilyIndex
				};
				try
				{
					commandPools.emplace_back(
								  device
								, poolInfo
								);
				}
				catch (const std::exception &)
				{
					throw;						// Re-throw the exception to be caught by the caller
				}
			}
		}
		
		vk::raii::CommandPool &getCommandPool(
			uint32_t		threadIndex
		)
		{
			std::lock_guard				lock(resourceMutex);
			return commandPools[threadIndex];
		}
	

//******************************************************************************************
// 
//  Name:           allocateCommandBuffers
//  Arguments:      vk::raii::Device &device
//		    uint32_t threadCount
//		    uint32_t buffersPerThread
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************
	
		void allocateCommandBuffers(
			vk::raii::Device 			&device
			, uint32_t				threadCount
			, uint32_t				buffersPerThread
		)
		{
			std::lock_guard				lock(resourceMutex);
			
			commandBuffers.clear();
			
			if (commandPools.size() < threadCount)
			{
				throw std::runtime_error("Not enough command pools for thread count...");
			}
			
			for (uint32_t i = 0; i < threadCount; i++)
			{
				vk::CommandBufferAllocateInfo	allocInfo
				{
					.commandPool					= *commandPools[i]
					, .level					= vk::CommandBufferLevel::ePrimary
					, .commandBufferCount				= buffersPerThread
				};
				try
				{
					auto threadBuffers				= device.allocateCommandBuffers(allocInfo);
					for (auto &buffer : threadBuffers)
					{
						commandBuffers.emplace_back(std::move(buffer));
					}
				}
				catch (const std::exception &)
				{
					throw;						// Re-throw the exception to be caught by the caller
				}
			}
		}
	

//******************************************************************************************
// 
//  Name:           allocateCommandBuffers
//  Arguments:      uint32_t index
//  Returns:        vk::raii::CommandBuffer &
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************
	
	vk::raii::CommandBuffer &getCommandBuffer(
		uint32_t index
	)
	{
		// No need for mutex here as each thread accesses its own command buffer
		if (index >= commandBuffers.size())
		{
			throw std::runtime_error("Command buffer index out of range: " + std::to_string(index) +
							" (available: " + std::to_string(commandBuffers.size()) + ")");
		}
		return commandBuffers[index];
	}
};


class MultithreadedApplication
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
	    initThreads();
            mainLoop();
            cleanup();
        }

    private:

        // Initial set up and swapchain
        GLFWwindow                              *window                     = nullptr;
	
	    // Vulkan objects
        vk::raii::Context                       context;
        vk::raii::Instance                      instance                    = nullptr;
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
        vk::raii::PipelineLayout                pipelineLayout              = nullptr;
        vk::raii::Pipeline                      graphicsPipeline            = nullptr;
	
	vk::raii::DescriptorSetLayout           computeDescriptorSetLayout  = nullptr;
	vk::raii::PipelineLayout		computePipelineLayout		= nullptr;
	vk::raii::Pipeline			computePipeline			= nullptr;

	// Shader Buffers
	std::vector<vk::raii::Buffer>		shaderStorageBuffers;
	std::vector<vk::raii::DeviceMemory>	shaderStorageBuffersMemory;
	
	// Uniform Buffers
	std::vector<vk::raii::Buffer>		uniformBuffers;
	std::vector<vk::raii::DeviceMemory>	uniformBuffersMemory;
	std::vector<void *>			uniformBuffersMapped;
	
        // Descriptor pool
        vk::raii::DescriptorPool                descriptorPool              = nullptr;
	std::vector<vk::raii::DescriptorSet> 	computeDescriptorSets;
	
        // Command pool
        vk::raii::CommandPool                   commandPool                 = nullptr;
        std::vector<vk::raii::CommandBuffer>    graphicsCommandBuffers;
	
        // Synchronization objects - Semaphores and fences
	vk::raii::Semaphore			timelineSemaphore		= nullptr;
	uint64_t				timelineValue			= 0;
        std::vector<vk::raii::Semaphore>        imageAvailableSemaphores;
        std::vector<vk::raii::Fence>            inFlightFences;
        uint32_t                                frameIndex                  = 0;
	
	double					lastFrameTime			= 0.0;
	
        bool 					framebufferResized          = false;

	double					lastTime			= 0.0f;
	
	uint32_t				threadCount			= 0;
	std::vector<std::thread>		workerThreads;
	std::atomic<bool>			shouldExit{false};
	std::vector<std::atomic<bool>>		threadWorkReady;
	std::vector<std::atomic<bool>>		threadWorkDone;
	
	std::mutex				queueSubmitMutex;
	std::mutex				workCompleteMutex;
	std::condition_variable			workCompleteCv;
	
	ThreadSafeResourceManager		resourceManager;
	struct					ParticleGroup
	{
		uint32_t			startIndex;
		uint32_t			count;
	};
	std::vector<ParticleGroup>		particleGroups;
	
	std::vector<const char *> 		requiredDeviceExtension		=
	{
		vk::KHRSwapchainExtensionName
	};
	
	// Helper functions


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
        
        [[nodiscard]] static std::vector<const char *> getRequiredInstanceExtensions()
        {
            // Get GLFW extensions
            uint32_t 		        glfwExtensionCount		= 0;
            auto			glfwExtensions			= glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
            std::vector			extensions(
                  glfwExtensions
                , glfwExtensions + glfwExtensionCount
            );
            
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

        static uint32_t chooseSwapMinImageCount(
		vk::SurfaceCapabilitiesKHR const &surfaceCapabilities
	)
        {
            auto minImageCount = std::max(  3u
                                          , surfaceCapabilities.minImageCount
	    );
	    
            if (    (0 < surfaceCapabilities.maxImageCount) 
                 && (surfaceCapabilities.maxImageCount < minImageCount))
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

        static vk::SurfaceFormatKHR chooseSwapSurfaceFormat(
		const std::vector<vk::SurfaceFormatKHR> &availableFormats
	)
        {
            assert(!availableFormats.empty());
            const auto formatIt                             = std::ranges::find_if(  
                                                                              availableFormats
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

        static vk::PresentModeKHR chooseSwapPresentMode(
		std::vector<vk::PresentModeKHR> const &availablePresentModes
	)
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
                int width, height;

                glfwGetFramebufferSize(
                      window
                    , &width
                    , &height
                );

            return
            {
                std::clamp<uint32_t>(  
                                  width
                                , capabilities.minImageExtent.width
                                , capabilities.maxImageExtent.width
                            )
                , std::clamp<uint32_t>(  
                                  height
                                , capabilities.minImageExtent.height
                                , capabilities.maxImageExtent.height
                            )
            };
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

	[[nodiscard]] vk::raii::ShaderModule createShaderModule(
		const std::vector<char> &code
	) const
	{
		vk::ShaderModuleCreateInfo	createInfo
		{
			  .codeSize						                = code.size()
			, .pCode							        = reinterpret_cast<const uint32_t *>(code.data())
		};
		
		vk::raii::ShaderModule		shaderModule{device, createInfo};
		
		return shaderModule;
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

    static std::vector<char> readFile(
	const std::string &filename
    )
    {
            std::ifstream file(  
				filename
                               , std::ios::ate 
			       | std::ios::binary
			);

            if (!file.is_open())
            {
                throw std::runtime_error("Failed to open file:" + filename);
            }

            std::vector<char> buffer(file.tellg());

            file.seekg(
		0
		, std::ios::beg
	    );
            file.read(
		buffer.data()
		, static_cast<std::streamsize>(buffer.size())
	    );
            file.close();
#endif
            return buffer;
        }

        
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

            window = glfwCreateWindow(
					 WIDTH
                                      , HEIGHT
                                      , "Vulkan MultiThreading"
                                      , nullptr
                                      , nullptr
                                    );

            glfwSetWindowUserPointer(  window
                                     , this);

            glfwSetFramebufferSizeCallback(  window
                                           , framebufferResizeCallback);
					   
	    lastTime								= glfwGetTime();
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
		auto app                                        = reinterpret_cast<MultithreadedApplication *>(glfwGetWindowUserPointer(window));

		if (app)
		{
		            app->framebufferResized                         = true;
		}
        }
	

//******************************************************************************************
// 
//  Name:           initVulkan
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//            	createInstance();
//            	createSurface();
//            	pickPhysicalDevice();
//            	createLogicalDevice();
//            	createSwapChain();
//            	createImageViews();
//            	createComputeDescriptorSetLayout();
//            	createGraphicsPipeline();
//	      	createComputePipeline();
//            	createCommandPool();
//	      	createShaderStorageBuffers();
//            	createUniformBuffers();
//            	createDescriptorPool();
//            	createComputeDescriptorSets();
//            	createGraphicsCommandBuffers();
//            	createSyncObjects();
//
//  Called by:      initVulkan
//  Description:    Control structure for initializing the Vulkan framework.
// 
//******************************************************************************************

        void initVulkan()
        {
            createInstance();
            createSurface();
            pickPhysicalDevice();
            createLogicalDevice();
            createSwapChain();
            createImageViews();
            createComputeDescriptorSetLayout();
            createGraphicsPipeline();
	    createComputePipeline();
            createCommandPool();
	    createShaderStorageBuffers();
            createUniformBuffers();
            createDescriptorPool();
            createComputeDescriptorSets();
            createGraphicsCommandBuffers();
            createSyncObjects();
        }
	
	
//******************************************************************************************
// 
//  Name:           initThreads
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

	void initThreads()
	{
		// Increase thread count for better parallelism
		threadCount							= 8u;
		log(
			"Initializing "
			, threadCount
			, " threads for sequential execution"
		);
		
		threadWorkReady							= std::vector<std::atomic<bool>>(threadCount);
		threadWorkDone							= std::vector<std::atomic<bool>>(threadCount);
		
		for (uint32_t i = 0; i < threadCount; i++)
		{
			threadWorkReady[i]				 	= false;
			threadWorkDone[i]					= true;
		}
		
		initThreadResources();
		
		const uint32_t			particlesPerThread		= PARTICLE_COUNT / threadCount;
		particleGroups.resize(threadCount);
		
		for (uint32_t i = 0; i < threadCount; i++)
		{
			particleGroups[i].startIndex				= i * particlesPerThread;
			particleGroups[i].count					= (i == threadCount - 1) ?
											(PARTICLE_COUNT - i * particlesPerThread) :
											particlesPerThread;
			log(
				"Thread "
				, i
				, " will process particles "
				, particleGroups[i].startIndex
				, " to "
				, (particleGroups[i].startIndex + particleGroups[i].count - 1)
				, " (count: "
				, particleGroups[i].count
				, ")"
			);
		}
		
		for (uint32_t i = 0; i < threadCount; i++)
		{
			workerThreads.emplace_back(
				&MultithreadedApplication::workerThreadFunc
				, this
				, i
			);
			log(
				"Started worker thread "
				, i
			);
		}
	}
	
	
//******************************************************************************************
// 
//  Name:           workerThreadFunc
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

	void workerThreadFunc(
		uint32_t		threadIndex
	)
	{
		while (!shouldExit)
		{
			// Wait for work using condition variable
			{
				std::unique_lock<std::mutex> 	lock(workCompleteMutex);
				workCompleteCv.wait(
					lock
					, [this
					, threadIndex]()
					{
						return shouldExit || threadWorkReady[threadIndex].load(std::memory_order_acquire);
					}
				);
				
				if (shouldExit)
				{
					break;
				}
				
				if (!threadWorkReady[threadIndex].load(std::memory_order_acquire))
				{
					continue;
				}
			}
			
			const ParticleGroup 		&group			= particleGroups[threadIndex];
			bool				workCompleted		= false;
			
			try
			{
				// Get command buffer and record commands
				vk::raii::CommandBuffer *cmdBuffer 		= &resourceManager.getCommandBuffer(threadIndex);
				recordComputeCommandBuffer(
					*cmdBuffer
					, group.startIndex
					, group.count
				);
				workCompleted					= true;
			}
			catch (const std::exception &)
			{
				workCompleted					= false;
			}
			
			// Mark work as done
			threadWorkDone[threadIndex].store(
				true
				, std::memory_order_release
			);
			threadWorkReady[threadIndex].store(
				false
				, std::memory_order_release
			);
			
			// If this is not the last thread, signal the next thread to start
			if (threadIndex < threadCount - 1)
			{
				threadWorkReady[threadIndex + 1].store(
					true
					, std::memory_order_release
				);
			}
			
			// Notify main thread and other threads
			{
				std::lock_guard<std::mutex> lock(workCompleteMutex);
				workCompleteCv.notify_all();
			}
		}
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

#if PLATFORM_DESKTOP
        void cleanup()
        {
		// Clean up resources in each GameObject
		for (auto &gameObject : gameObjects)
		{
			// Unmap memory
			for (size_t i = 0; i < gameObject.uniformBuffersMemory.size(); i ++)
			{
				if (gameObject.uniformBuffersMapped[i] != nullptr)
				{
					gameObject.uniformBuffersMemory[i].unmapMemory();
				}
			}
			
			// Clear vectors to release resources
			gameObject.uniformBuffers.clear();
			gameObject.uniformBuffersMemory.clear();
			gameObject.uniformBuffersMapped.clear();
			gameObject.descriptorSets.clear();
		}

	    // Clean up GLFW resources
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
            auto                    extensions              = getRequiredInstanceExtensions();
            
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
            bool                        supportsGraphics    = std::ranges::any_of(  
                                                                                queueFamilies
                                                                                , [](auto const &qfp)
                                                                                {
                                                                                    return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics);
                                                                                }
                                                                            );

            // Check if all required physicalDevice extensions are available
            auto            availableDeviceExtensions       = physicalDevice.enumerateDeviceExtensionProperties();
            bool            supportsAllRequiredExtensions   = 
                                            std::ranges::all_of(
                                                                  requiredDeviceExtension
                                                                , [&availableDeviceExtensions](auto const &requiredDeviceExtension)
                                                                {
                                                                    return std::ranges::any_of(
                                                                                              availableDeviceExtensions
                                                                                            , [requiredDeviceExtension](auto const &availableDeviceExtension)
                                                                                                {
                                                                                                    return strcmp(
                                                                                                              availableDeviceExtension.extensionName
                                                                                                            , requiredDeviceExtension) == 0;
                                                                                                }
                                                                                            );
                                                                });

            // Check if the physicalDevice supports the required features
	    
            auto 			            features			        = physicalDevice
									                                    .template getFeatures2<
                                                                            vk::PhysicalDeviceFeatures2
                                                                            , vk::PhysicalDeviceVulkan13Features
                                                                            , vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT
                                                                        >();
            bool 			            supportsRequiredFeatures	= features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
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
            strcpy(
                  profileProperties.name
                , VP_KHR_ROADMAP_2022_NAME
            );
#else	    
            strcpy(
                  profileProperties.profileName
                , VP_KHR_ROADMAP_2022_NAME
            );
#endif
            profileProperties.specVersion			                = VP_KHR_ROADMAP_2022_SPEC_VERSION;
        
            VkBool32	                supported	                = VK_FALSE;
            bool		                result			            = false;
			
#if PLATFORM_ANDROID
			// Create a vp::ProfileDesc from our VpProfileProperties
			vp::ProfileDesc			    profileDesc		            = 
			{
				  profileProperties.name
				, profileProperties.specVersion
			};
			
			// Use vp::GetProfileSupport for Android
			result			                                        = vp::GetProfileSupport(
				  *physicalDevice			// Pass the physical device directly
				, &profileDesc			    // Pass the profile description
				, &supported			    // Output parameter for support status
			);
#else
			// Use vpGetPhysicalDeviceProfileSupport for Desktop
			VkResult	                vk_result		  	        = vpGetPhysicalDeviceProfileSupport(
                                                                                          *instance
                                                                                        , *physicalDevice
                                                                                        , &profileProperties
                                                                                        , &supported
                                                                                    );
											    
			result				                                    = vk_result == static_cast<int>(vk::Result::eSuccess);
#endif
			const char                  *name 	                    = nullptr;
#ifdef PLATFORM_ANDROID
			name			                                        = profileProperties.name;
#else
			name			                                        = profileProperties.profileName;
#endif

			if (result && supported == VK_TRUE)
			{
				appInfo.profileSupported	                        = true;
				appInfo.profile					                    = profileProperties;
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
            auto							                    features 		= physicalDevice.getFeatures2();
            vk::PhysicalDeviceVulkan13Features			        vulkan13Features;
            vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT	extendedDynamicStateFeatures;
            vulkan13Features.dynamicRendering						            = vk::True;
            vulkan13Features.synchronization2						            = vk::True;
            extendedDynamicStateFeatures.extendedDynamicState 				    = vk::True;
            vulkan13Features.pNext								                = &extendedDynamicStateFeatures;
            features.pNext									                    = &vulkan13Features;
            
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
                    , .enabledExtensionCount                = static_cast<uint32_t>(requiredDeviceExtension.size())
                    , .ppEnabledExtensionNames              = requiredDeviceExtension.data()
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
            vk::SurfaceCapabilitiesKHR      surfaceCapabilities     = physicalDevice.getSurfaceCapabilitiesKHR(*surface);
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
                      .viewType				                = vk::ImageViewType::e2D
                    , .format				                = swapChainSurfaceFormat.format
                    , .subresourceRange			            = 
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
            std::array bindings			                    = 
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
            vk::raii::ShaderModule		    shaderModule		    = createShaderModule(this->readFile("shaders/slang.spv"));
            
            vk::PipelineShaderStageCreateInfo   vertShaderStageInfo
            {
                  .stage						                    = vk::ShaderStageFlagBits::eVertex
                , .module					                        = *shaderModule
                , .pName					                        = "vertMain"
            };
            
            vk::PipelineShaderStageCreateInfo	fragShaderStageInfo
            {
                  .stage						                    = vk::ShaderStageFlagBits::eFragment
                , .module					                        = *shaderModule
                , .pName					                        = "fragMain"
            };

            // Create shader stages
            vk::PipelineShaderStageCreateInfo   shaderStages[]      = 
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
                , .frontFace                                        = vk::FrontFace::eCounterClockwise		// Keeping Clockwise for glTF
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
                , .depthBoundsTestEnable			                = vk::False
                , .stencilTestEnable				                = vk::False
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
            std::vector                     dynamicStates           = 
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
		        , .pushConstantRangeCount			                = 0
            };

            pipelineLayout                                          = vk::raii::PipelineLayout(device, pipelineLayoutInfo);

            vk::Format			            depthFormat		        = findDepthFormat();

            // Create the graphics pipeline
            vk::StructureChain<
                  vk::GraphicsPipelineCreateInfo
                , vk::PipelineRenderingCreateInfo
            >						        pipelineCreateInfoChain	= 
            {
                {
                      .stageCount                                   = 2
                    , .pStages                                      = shaderStages
                    , .pVertexInputState                            = &vertexInputInfo
                    , .pInputAssemblyState                          = &inputAssembly
                    , .pViewportState                               = &viewportState
                    , .pRasterizationState                          = &rasterizer
                    , .pMultisampleState                            = &multisampling
                    , .pDepthStencilState                           = &depthStencil
                    , .pColorBlendState                             = &colorBlending
                    , .pDynamicState                                = &dynamicState
                    , .layout                                       = *pipelineLayout
                    , .renderPass                                   = nullptr
                }
                ,
                {
                      .colorAttachmentCount				            = 1
                    , .pColorAttachmentFormats			            = &swapChainSurfaceFormat.format
                    , .depthAttachmentFormat			            = depthFormat
                }
            };

            graphicsPipeline = vk::raii::Pipeline(
                  device
                , nullptr
                , pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>()
		    );
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
            vk::CommandPoolCreateInfo       poolInfo
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
            vk::Format 			            depthFormat     = findDepthFormat();

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
              const std::vector<vk::Format>     &candidates
            , vk::ImageTiling                   tiling
            , vk::FormatFeatureFlags            features
        ) const
        {
            for (const auto format : candidates)
            {
                vk::FormatProperties            props           = physicalDevice.getFormatProperties(format);

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
            return     format == vk::Format::eD32SfloatS8Uint 
                    || format == vk::Format::eD24UnormS8Uint;
        }
        

//******************************************************************************************
// 
//  Name:           createTextureImage
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void createTextureImage()
        {
            // Load KTX2 texture instead of using stb_image
            ktxTexture	*kTexture;
            KTX_error_code	            result		        = 	ktxTexture_CreateFromNamedFile(
                                                                            TEXTURE_PATH.c_str()
                                                                            , KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT
                                                                            , &kTexture
                                                                    );

            if (result != KTX_SUCCESS)
            {
                throw std::runtime_error("Failed to load ktx texture image!");
            }

            // Get texture dimensions and data
            uint32_t			        texWidth			= kTexture->baseWidth;
            uint32_t			        texHeight			= kTexture->baseHeight;
            ktx_size_t			        imageSize			= ktxTexture_GetImageSize(kTexture, 0);
            ktx_uint8_t			        *ktxTextureData	    = ktxTexture_GetData(kTexture);
	    

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

            void                        *data               = stagingBufferMemory.mapMemory(0, imageSize);

            memcpy(  
                  data
                , ktxTextureData
                , imageSize
            );

            stagingBufferMemory.unmapMemory();

            // Determine the Vulkan format from KTX format
	        vk::Format		            textureFormat;
	    
            if (kTexture->classId == ktxTexture2_c)
            {
                // For KTX2 files, we can get the format directly
                auto                    *ktx2				= reinterpret_cast<ktxTexture2 *>(kTexture);
                textureFormat						        = static_cast<vk::Format>(ktx2->vkFormat);
                if (textureFormat == vk::Format::eUndefined)
                {
                    // If the format is undefined, fall backto a reasonable default
                    textureFormat					        = vk::Format::eR8G8B8A8Unorm;
                }
            }
            else
            {
                // For KTX1 files or if we can't determine the format, use a reasonable default
                textureFormat						        = vk::Format::eR8G8B8A8Unorm;
            }
	    
	        textureImageFormat						        = textureFormat;
	    
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
		    vk::PhysicalDeviceProperties 	properties	        = physicalDevice.getProperties();
            vk::SamplerCreateInfo           samplerInfo
            {
                  .magFilter                                    = vk::Filter::eLinear
                , .minFilter                                    = vk::Filter::eLinear
                , .mipmapMode                                   = vk::SamplerMipmapMode::eLinear
                , .addressModeU                                 = vk::SamplerAddressMode::eRepeat
                , .addressModeV                                 = vk::SamplerAddressMode::eRepeat
                , .addressModeW                                 = vk::SamplerAddressMode::eRepeat
		, .mipLodBias				                    = 0.0f
                , .anisotropyEnable                             = vk::True
                , .maxAnisotropy                                = properties.limits.maxSamplerAnisotropy
                , .compareEnable                                = vk::False
                , .compareOp                                    = vk::CompareOp::eAlways
            };

            textureSampler                                      = vk::raii::Sampler(device, samplerInfo);
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

        vk::raii::ImageView createImageView(
              vk::raii::Image &image
            , vk::Format format
            , vk::ImageAspectFlags aspectFlags
        )
        {
            vk::ImageViewCreateInfo             viewInfo
            {
                  .image                                        = *image
                , .viewType                                     = vk::ImageViewType::e2D
                , .format                                       = format
                , .subresourceRange                             = 
                {
                      aspectFlags
                    , 0
                    , 1
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
              uint32_t                      width
            , uint32_t                      height
            , vk::Format                    format
            , vk::ImageTiling               tiling
            , vk::ImageUsageFlags           usage
            , vk::MemoryPropertyFlags       properties
            , vk::raii::Image               &image
            , vk::raii::DeviceMemory        &imageMemory
        )
        {
            vk::ImageCreateInfo             imageInfo
            {
                  .imageType                                    = vk::ImageType::e2D
                , .format                                       = format
                , .extent                                       = 
                { 
                      width
                    , height
                    , 1 
                }
                , .mipLevels                                    = 1
                , .arrayLayers                                  = 1
                , .samples                                      = vk::SampleCountFlagBits::e1
                , .tiling                                       = tiling
                , .usage                                        = usage
                , .sharingMode                                  = vk::SharingMode::eExclusive
                , .initialLayout                                = vk::ImageLayout::eUndefined
            };

            image                                               = vk::raii::Image(device, imageInfo);
            
            vk::MemoryRequirements          memRequirements     = image.getMemoryRequirements();
	    
            vk::MemoryAllocateInfo          allocInfo
            {
                      .allocationSize                           = memRequirements.size
                    , .memoryTypeIndex                          = findMemoryType(
                                                                                  memRequirements.memoryTypeBits
                                                                                , properties
                                                                            )
            };
	    
            imageMemory                                         = vk::raii::DeviceMemory(device, allocInfo);
            image.bindMemory(*imageMemory, 0);
        }
        

//******************************************************************************************
// 
//  Name:           transitionImageLayout
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void transitionImageLayout(
              const vk::raii::Image         &image
            , vk::ImageLayout               oldLayout
            , vk::ImageLayout               newLayout
        )
        {
		    auto                            commandBuffer		= beginSingleTimeCommands();

            vk::ImageMemoryBarrier  barrier         
            {
                  .oldLayout                                    = oldLayout
                , .newLayout                                    = newLayout
                , .image                                        = *image
                , .subresourceRange                             = 
                {
			          vk::ImageAspectFlagBits::eColor
                    , 0
                    , 1
                    , 0
                    , 1
                }
            };

            vk::PipelineStageFlags sourceStage;
            vk::PipelineStageFlags destinationStage;

            if (   oldLayout == vk::ImageLayout::eUndefined 
                && newLayout == vk::ImageLayout::eTransferDstOptimal)
            {
                barrier.srcAccessMask                           = {};
                barrier.dstAccessMask                           = vk::AccessFlagBits::eTransferWrite;

                sourceStage                                     = vk::PipelineStageFlagBits::eTopOfPipe;
                destinationStage                                = vk::PipelineStageFlagBits::eTransfer;
            }
            else if (   oldLayout == vk::ImageLayout::eTransferDstOptimal
                     && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal)
            {
                barrier.srcAccessMask                           = vk::AccessFlagBits::eTransferWrite;
                barrier.dstAccessMask                           = vk::AccessFlagBits::eShaderRead;

                sourceStage                                     = vk::PipelineStageFlagBits::eTransfer;
                destinationStage                                = vk::PipelineStageFlagBits::eFragmentShader;
            }
            else
            {
                throw std::invalid_argument("Unsupported layout transition!");
            }

            commandBuffer->pipelineBarrier(
                  sourceStage
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
		      const vk::raii::Buffer    	&buffer
            , vk::raii::Image     	        &image
            , uint32_t      		        width
            , uint32_t      		        height
        )
        {
	        std::unique_ptr<vk::raii::CommandBuffer> commandBuffer = beginSingleTimeCommands();
            vk::BufferImageCopy             region
            {
                  .bufferOffset                                     = 0
                , .bufferRowLength                                  = 0
                , .bufferImageHeight                                = 0
                , .imageSubresource                                 =
                {
                      vk::ImageAspectFlagBits::eColor
                    , 0
                    , 0
                    , 1
                }
                , .imageOffset                                      = { 0, 0, 0 }
                , .imageExtent                                      = { width, height, 1}
            };

            commandBuffer->copyBufferToImage(
                  *buffer
                , *image
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
            // Use tinygltf to load the model instead of tinyobjloader
            tinygltf::Model			        model;
            tinygltf::TinyGLTF		        loader;
            std::string			            err;
            std::string			            warn;
            
            bool 				            ret			            = loader.LoadBinaryFromFile(
                                                                                  &model
                                                                                , &err
                                                                                , &warn
                                                                                , MODEL_PATH
                                                                            );
            
            if (!warn.empty())
            {
                std::cout << "glTF warning: " << warn << std::endl;
            }
            
            if (!err.empty())
            {
                std::cout << "glTF error: " << err << std::endl;
            }
            
            if (!ret)
            {
                throw std::runtime_error("Failed to load glTF model");
            }
            
            vertices.clear();
            indices.clear();
            
            // Process all meshes in the model
            for (const auto &mesh : model.meshes)
            {
                for (const auto &primitive : mesh.primitives)
                {
                    // Get indices
                    const tinygltf::Accessor		&indexAccessor		= model.accessors[primitive.indices];
                    const tinygltf::BufferView		&indexBufferView	= model.bufferViews[indexAccessor.bufferView];
                    const tinygltf::Buffer			&indexBuffer		= model.buffers[indexBufferView.buffer];
                    
                    // Get vertex positions
                    const tinygltf::Accessor		&posAccessor		= model.accessors[primitive.attributes.at("POSITION")];
                    const tinygltf::BufferView		&posBufferView		= model.bufferViews[posAccessor.bufferView];
                    const tinygltf::Buffer			&posBuffer		    = model.buffers[posBufferView.buffer];
                    
                    // Get texture coordinates if available
                    bool 					        hasTexCoords		= primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end();
                    const tinygltf::Accessor		*texCoordAccessor	= nullptr;
                    const tinygltf::BufferView		*texCoordBufferView	= nullptr;
                    const tinygltf::Buffer			*texCoordBuffer		= nullptr;
                    
                    if (hasTexCoords)
                    {
                        texCoordAccessor					            = &model.accessors[primitive.attributes.at("TEXCOORD_0")];
                        texCoordBufferView					            = &model.bufferViews[texCoordAccessor->bufferView];
                        texCoordBuffer						            = &model.buffers[texCoordBufferView->buffer];
                    }
                    
                    uint32_t                        baseVertex			= static_cast<uint32_t>(vertices.size());
                    
                    for (size_t i = 0; i < posAccessor.count; i++)
                    {
                        Vertex vertex{};
                        
                        const float                 *pos				= reinterpret_cast<const float *>(&posBuffer.data[posBufferView.byteOffset + posAccessor.byteOffset + i * 12]);
                        // glTF uses a right-handed coordinate system with Y-up
                        // Vulkan uses a right-handed coordinate system with Y-down
                        // We need to flip the Y coordinate
                        vertex.pos						                = {pos[0], pos[1], pos[2]};
                        
                        if (hasTexCoords)
                        {
                            const float             *texCoord			= reinterpret_cast<const float *>(&texCoordBuffer->data[texCoordBufferView->byteOffset + texCoordAccessor->byteOffset + i * 8]);
                            vertex.texCoord					            = {texCoord[0], texCoord[1]};
                        }
                        else
                        {
                            vertex.texCoord					            = {0.0f, 0.0f};
                        }
                        
                        vertex.color = {1.0f, 1.0f, 1.0f};
                        
                        vertices.push_back(vertex);
                    }
                    
                    const unsigned char             *indexData			= &indexBuffer.data[indexBufferView.byteOffset + indexAccessor.byteOffset];
                    size_t				            indexCount			= indexAccessor.count;
                    size_t				            indexStride			= 0;
                    
                    // Determine index stride based on component type
                    if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                    {
                        indexStride						                = sizeof(uint16_t);
                    }
                    else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
                    {
                        indexStride						                = sizeof(uint32_t);
                    }
                    else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
                    {
                        indexStride						                = sizeof(uint8_t);
                    }
                    else
                    {
                        throw std::runtime_error("Unsupported index component type");
                    }
                    
                    indices.reserve(indices.size() + indexCount);
                    
                    for (size_t i = 0; i < indexCount; i++)
                    {
                        uint32_t		            index				= 0;
                        
                        if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                        {
                            index						                = *reinterpret_cast<const uint16_t *>(indexData + i * indexStride);
                        }
                        else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
                        {
                            index						                = *reinterpret_cast<const uint32_t *>(indexData + i * indexStride);
                        }
                        else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
                        {
                            index						                = *reinterpret_cast<const uint8_t *>(indexData + i * indexStride);
                        }
                        
                        indices.push_back(baseVertex + index);
                    }
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
            vk::DeviceSize                  bufferSize              = sizeof(vertices[0]) * vertices.size();
            
            vk::raii::Buffer                stagingBuffer({});
            vk::raii::DeviceMemory          stagingBufferMemory({});

            createBuffer(
                  bufferSize
                , vk::BufferUsageFlagBits::eTransferSrc
                , vk::MemoryPropertyFlagBits::eHostVisible
                | vk::MemoryPropertyFlagBits::eHostCoherent
                , stagingBuffer
                , stagingBufferMemory
            );

            void                            *dataStaging			= stagingBufferMemory.mapMemory(0, bufferSize);

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
            vk::DeviceSize                  bufferSize              = sizeof(indices[0]) * indices.size();

            vk::raii::Buffer                stagingBuffer({});
            vk::raii::DeviceMemory          stagingBufferMemory({});

            createBuffer(
                  bufferSize
                , vk::BufferUsageFlagBits::eTransferSrc
                , vk::MemoryPropertyFlagBits::eHostVisible
                | vk::MemoryPropertyFlagBits::eHostCoherent
                , stagingBuffer
                , stagingBufferMemory
            );

            void                            *data 		            = stagingBufferMemory.mapMemory(0, bufferSize);

            memcpy(
                  data
                , indices.data()
                , bufferSize
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
//  Name:           setupGameObjects
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

		// Initialize the game objects with different positions, rotations, and scales
		void setupGameObjects()
		{
			// Object 1 - Center
			gameObjects[0].position					= {0.0f, 0.0f, 0.0f};
			gameObjects[0].rotation					= {0.0f, glm::radians(-90.0f), 0.0f};
			gameObjects[0].scale					= {1.0f, 1.0f, 1.0f};
			
			// Object 2 - Left
			gameObjects[1].position					= {-2.0f, 0.0f, -1.0f};
			gameObjects[1].rotation					= {0.0f, glm::radians(-45.0f), 0.0f};
			gameObjects[1].scale					= {0.75f, 0.75f, 0.75f};
			
			// Object 3 - Right
			gameObjects[2].position					= {2.0f, 0.0f, -1.0f};
			gameObjects[2].rotation					= {0.0f, glm::radians(45.0f), 0.0f};
			gameObjects[2].scale					= {0.75f, 0.75f, 0.75f};
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

	// Create uniform buffers for each object
        void createUniformBuffers()
        {
		// For each game object
		for (auto &gameObject : gameObjects)
		{
	            gameObject.uniformBuffers.clear();
	            gameObject.uniformBuffersMemory.clear();
		        gameObject.uniformBuffersMapped.clear();

		// Create uniform buffers for each frame in flight
	            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	            {
	                vk::DeviceSize              bufferSize              = sizeof(UniformBufferObject);
	                vk::raii::Buffer		    buffer({});
	                vk::raii::DeviceMemory		bufferMem({});
	
	                createBuffer(
	                      bufferSize
	                    , vk::BufferUsageFlagBits::eUniformBuffer
	                    , vk::MemoryPropertyFlagBits::eHostVisible
	                    | vk::MemoryPropertyFlagBits::eHostCoherent
		            , buffer
	                    , bufferMem
	                );

	                gameObject.uniformBuffers.emplace_back(std::move(buffer));
	                gameObject.uniformBuffersMemory.emplace_back(std::move(bufferMem));
	                gameObject.uniformBuffersMapped.emplace_back(
	                                    gameObject.uniformBuffersMemory[i].mapMemory(0, bufferSize)
	                                );
			}
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
		// We need MAX_OBJECTS * MAX_FRAMES_IN_FLIGHT descriptor sets
            std::array poolSize
            {
                vk::DescriptorPoolSize
                (
                      vk::DescriptorType::eUniformBuffer
                    , MAX_OBJECTS * MAX_FRAMES_IN_FLIGHT
                )
                , vk::DescriptorPoolSize
                (
                      vk::DescriptorType::eCombinedImageSampler
                    , MAX_OBJECTS * MAX_FRAMES_IN_FLIGHT
                )
            };

            vk::DescriptorPoolCreateInfo    poolInfo
            {
                  .flags				                    = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet
                , .maxSets                                  = MAX_OBJECTS * MAX_FRAMES_IN_FLIGHT
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
		// For each game object
		for (auto &gameObject : gameObjects)
		{
			// Create descriptor sets for each frame in flight
            std::vector<vk::DescriptorSetLayout>    layouts(  MAX_FRAMES_IN_FLIGHT
                                                            , *descriptorSetLayout);

            vk::DescriptorSetAllocateInfo           allocInfo
            {
                  .descriptorPool                           = *descriptorPool
                , .descriptorSetCount                       = static_cast<uint32_t>(layouts.size())
                , .pSetLayouts                              = layouts.data()
            };

	        gameObject.descriptorSets.clear();
                gameObject.descriptorSets                                  = device.allocateDescriptorSets(allocInfo);

            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            {
                vk::DescriptorBufferInfo            bufferInfo
                {
                      .buffer                               = *gameObject.uniformBuffers[i]
                    , .offset                               = 0
                    , .range                                = sizeof(UniformBufferObject)
                };

                vk::DescriptorImageInfo             imageInfo
                {
                      .sampler                              = *textureSampler
                    , .imageView                            = *textureImageView
                    , .imageLayout                          = vk::ImageLayout::eShaderReadOnlyOptimal
                };

                std::array descriptorWrites
                {
                    vk::WriteDescriptorSet
                    {
                          .dstSet                           = *gameObject.descriptorSets[i]
                        , .dstBinding                       = 0
                        , .dstArrayElement                  = 0
                        , .descriptorCount                  = 1
                        , .descriptorType                   = vk::DescriptorType::eUniformBuffer
                        , .pBufferInfo                      = &bufferInfo
                    }
		    
                    , vk::WriteDescriptorSet
                    {
                          .dstSet                           = *gameObject.descriptorSets[i]
                        , .dstBinding                       = 1
                        , .dstArrayElement                  = 0
                        , .descriptorCount                  = 1
                        , .descriptorType                   = vk::DescriptorType::eCombinedImageSampler
                        , .pImageInfo                       = &imageInfo
                    }
                };

                device.updateDescriptorSets(  descriptorWrites
                                            , {});
		}
            }
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

            buffer                                          = vk::raii::Buffer(device, bufferInfo);

            vk::MemoryRequirements      memRequirements     = buffer.getMemoryRequirements();

            vk::MemoryAllocateInfo      allocInfo
            {
                  .allocationSize                           = memRequirements.size
                , .memoryTypeIndex                          = findMemoryType(
                                                                               memRequirements.memoryTypeBits
                                                                             , properties
                                                                            )
            };

            bufferMemory                                    = vk::raii::DeviceMemory(device, allocInfo);
            
            buffer.bindMemory(*bufferMemory, 0);
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
		vk::CommandBufferAllocateInfo	allocInfo
		{
			  .commandPool						            = *commandPool
			, .level						                = vk::CommandBufferLevel::ePrimary
			, .commandBufferCount					        = 1
		};
		
		std::unique_ptr<vk::raii::CommandBuffer>    
                                        commandBuffer	    = std::make_unique<vk::raii::CommandBuffer>(std::move(vk::raii::CommandBuffers(device, allocInfo).front()));
		
		vk::CommandBufferBeginInfo		beginInfo
		{
			.flags							                = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
		};
		
		commandBuffer->begin(beginInfo);
		
		return commandBuffer;
	}
        

//******************************************************************************************
// 
//  Name:           endSingleTimeCommands
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

	void endSingleTimeCommands(const vk::raii::CommandBuffer &commandBuffer) const
	{
		commandBuffer.end();
		
		vk::SubmitInfo submitInfo
		{
			  .commandBufferCount				                    = 1
			, .pCommandBuffers					                    = &*commandBuffer
		};
		
		queue.submit(submitInfo, nullptr);
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
              vk::raii::Buffer              &srcBuffer
            , vk::raii::Buffer              &dstBuffer
            , vk::DeviceSize                size
        )
        {
            vk::CommandBufferAllocateInfo   allocInfo
            {
                  .commandPool					                    = *commandPool
                , .level					                        = vk::CommandBufferLevel::ePrimary
                , .commandBufferCount				                = 1
            };
            
            vk::raii::CommandBuffer		    commandCopyBuffer	    = std::move(device.allocateCommandBuffers(allocInfo).front());
            
            commandCopyBuffer.begin(vk::CommandBufferBeginInfo
                {
                    .flags						                    = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
                }
            );
        
                commandCopyBuffer.copyBuffer(
                      *srcBuffer
                    , *dstBuffer
                    , vk::BufferCopy{.size = size}
                );

            commandCopyBuffer.end();
                
            queue.submit(vk::SubmitInfo
                {
                      .commandBufferCount		                    = 1
                    , .pCommandBuffers		                        = &*commandCopyBuffer
                }
                , nullptr
            );

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
              uint32_t                      typeFilter
            , vk::MemoryPropertyFlags       properties
        )
        {
            vk::PhysicalDeviceMemoryProperties      memProperties   = physicalDevice.getMemoryProperties();

            for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
            {
                if (    (typeFilter & (1 << i)) 
                    &&  (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
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
                  .commandPool                              = *commandPool
                , .level                                    = vk::CommandBufferLevel::ePrimary
                , .commandBufferCount                       = MAX_FRAMES_IN_FLIGHT
            };

            commandBuffers                                  = vk::raii::CommandBuffers(device, allocInfo);
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
            uint32_t imageIndex
        )
        {
	        auto                    &commandBuffer			    = commandBuffers[frameIndex];
            commandBuffer.begin({});
	    
	    // Before starting rendering, transition the swapchain image to COLOR_ATTACHMENT_OPTIMAL
            transition_image_layout(
                  swapChainImages[imageIndex]
                , vk::ImageLayout::eUndefined
                , vk::ImageLayout::eColorAttachmentOptimal
                , {}							                        // srcAccessMask (no need to wait for previous operations)
                , vk::AccessFlagBits2::eColorAttachmentWrite		    // dstAccessMask
                , vk::PipelineStageFlagBits2::eColorAttachmentOutput	//srcStage
                , vk::PipelineStageFlagBits2::eColorAttachmentOutput	// dstStage
                , vk::ImageAspectFlagBits::eColor
            );
	
            // Transition depth image to depth attachment optimal layout
            transition_image_layout(
                  *depthImage
                , vk::ImageLayout::eUndefined
                , vk::ImageLayout::eDepthAttachmentOptimal
                , vk::AccessFlagBits2::eDepthStencilAttachmentWrite
                , vk::AccessFlagBits2::eDepthStencilAttachmentWrite
                , vk::PipelineStageFlagBits2::eEarlyFragmentTests
                | vk::PipelineStageFlagBits2::eLateFragmentTests
                , vk::PipelineStageFlagBits2::eEarlyFragmentTests
                | vk::PipelineStageFlagBits2::eLateFragmentTests
                , vk::ImageAspectFlagBits::eDepth
            );
	
            vk::ClearValue                  clearColor		   = vk::ClearColorValue(  
                          0.0f
                        , 0.0f
                        , 0.0f
                        , 1.0f
                    );

            vk::RenderingAttachmentInfo		attachmentInfo		=
            {
                  .imageView						            = *swapChainImageViews[imageIndex]
                , .imageLayout						            = vk::ImageLayout::eColorAttachmentOptimal
                , .loadOp						                = vk::AttachmentLoadOp::eClear
                , .storeOp						                = vk::AttachmentStoreOp::eStore
                , .clearValue						            = clearColor
            };
		
            vk::ClearValue				    clearDepth		    = vk::ClearDepthStencilValue{1.0f, 0};
            
            vk::RenderingAttachmentInfo		depthAttachmentInfo
            {
                  .imageView						            = *depthImageView
                , .imageLayout						            = vk::ImageLayout::eDepthStencilAttachmentOptimal
                , .loadOp						                = vk::AttachmentLoadOp::eClear
                , .storeOp						                = vk::AttachmentStoreOp::eDontCare
                , .clearValue						            = clearDepth
            };
            
            vk::RenderingInfo			    renderingInfo		=
            {
                  .renderArea						            = {.offset = {0, 0}, .extent = swapChainExtent}
                , .layerCount						            = 1
                , .colorAttachmentCount					        = 1
                , .pColorAttachments					        = &attachmentInfo
                , .pDepthAttachment					            = &depthAttachmentInfo
            };
		
		    commandBuffer.beginRendering(renderingInfo);

            commandBuffer.bindPipeline(
                  vk::PipelineBindPoint::eGraphics
                , *graphicsPipeline
            );

            commandBuffer.setViewport(
                  0
                , vk::Viewport(
                      0.0f
                    , 0.0f
                    , static_cast<float>(swapChainExtent.width)
                    , static_cast<float>(swapChainExtent.height)
                    , 0.0f
                    , 1.0f
                )
            );
            
            commandBuffer.setScissor(
                  0
                , vk::Rect2D(
                          vk::Offset2D(0, 0)
                        , swapChainExtent
                    )
            );

		// Bind vertex and index buffers (shared by all objects)
            commandBuffer.bindVertexBuffers(  
                  0
                , *vertexBuffer
                , {0}
            );

            commandBuffer.bindIndexBuffer(  
                  *indexBuffer
                , 0
                , vk::IndexType::eUint32
            );

	    // Draw each object with its own descriptor set
	    for (const auto &gameObject : gameObjects)
	    {
		// Bind the descriptor set for this object
            commandBuffer.bindDescriptorSets(
                  vk::PipelineBindPoint::eGraphics
                , *pipelineLayout
                , 0
                , *gameObject.descriptorSets[frameIndex]
                , nullptr
            );

		// Draw the object
            commandBuffer.drawIndexed(
                  indices.size()
                , 1
                , 0
                , 0
                , 0
            );
	    }

	        commandBuffer.endRendering();

            // After rendering, transition the swapchain image to PRESENT_SRC
            transition_image_layout(
                  swapChainImages[imageIndex]
                , vk::ImageLayout::eColorAttachmentOptimal
                , vk::ImageLayout::ePresentSrcKHR
                , vk::AccessFlagBits2::eColorAttachmentWrite		    // srcAccessMask
                , {}							                        // dstAccessMask
                , vk::PipelineStageFlagBits2::eColorAttachmentOutput	// srcStage
                , vk::PipelineStageFlagBits2::eBottomOfPipe		        // dstStage
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
              vk::Image			            image
            , vk::ImageLayout		        old_layout
            , vk::ImageLayout		        new_layout
            , vk::AccessFlags2		        src_access_mask
            , vk::AccessFlags2		        dst_access_mask
            , vk::PipelineStageFlags2	    src_stage_mask
            , vk::PipelineStageFlags2	    dst_stage_mask
            , vk::ImageAspectFlags		    image_aspect_flags
        )
        {
            vk::ImageMemoryBarrier2 	    barrier				    =
            {
                  .srcStageMask						                = src_stage_mask
                , .srcAccessMask					                = src_access_mask
                , .dstStageMask						                = dst_stage_mask
                , .dstAccessMask					                = dst_access_mask
                , .oldLayout						                = old_layout
                , .newLayout						                = new_layout
                , .srcQueueFamilyIndex					            = VK_QUEUE_FAMILY_IGNORED
                , .dstQueueFamilyIndex					            = VK_QUEUE_FAMILY_IGNORED
                , .image						                    = image
                , .subresourceRange					                =
                {
                      .aspectMask					                = image_aspect_flags
                    , .baseMipLevel					                = 0
                    , .levelCount					                = 1
                    , .baseArrayLayer				                = 0
                    , .layerCount					                = 1
                }
            };
            
            vk::DependencyInfo		        dependency_info			=
            {
                  .dependencyFlags					                = {}
                , .imageMemoryBarrierCount				            = 1
                , .pImageMemoryBarriers					            = &barrier
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
            assert(
                presentCompleteSemaphores.empty()
                && renderFinishedSemaphores.empty()
                && inFlightFences.empty()
            );

            for (size_t i = 0; i < swapChainImages.size(); i++)
            {
                renderFinishedSemaphores.emplace_back(device, vk::SemaphoreCreateInfo());
            }
	    
            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            {
                presentCompleteSemaphores.emplace_back(device, vk::SemaphoreCreateInfo());
                inFlightFences.emplace_back(
                      device
                    , vk::FenceCreateInfo
                    {
                        .flags						        = vk::FenceCreateFlagBits::eSignaled
                    }
                );
            }
        }
        

//******************************************************************************************
// 
//  Name:           updateUniformBuffers
//  Arguments:      N/A
//  Returns:        void
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

        void updateUniformBuffers()
        {
            static auto             startTime               = std::chrono::high_resolution_clock::now();
	    static auto 		lastFrameTime		= startTime;

            auto                    currentTime             = std::chrono::high_resolution_clock::now();
            float                   time                    = std::chrono::duration<float>(currentTime - startTime).count();
	    
	    float 			deltaTime		= std::chrono::duration<float>(currentTime - lastFrameTime).count();
	    lastFrameTime					= currentTime;

	    // Camera and projection matrices (shared by all objects)
            glm::mat4			view                = glm::lookAt(
                                                                      glm::vec3(2.0f, 2.0f, 6.0f)
                                                                    , glm::vec3(0.0f, 0.0f, 0.0f)
                                                                    , glm::vec3(0.0f, 1.0f, 0.0f)
                                                                );

            glm::mat4			proj                = glm::perspective(
                                                                      glm::radians(45.0f)
                                                                    , static_cast<float>(swapChainExtent.width) / static_cast<float>(swapChainExtent.height)
                                                                    , 0.1f
                                                                    , 20.0f
                                                                );

            proj[1][1] *= -1;

		// Update uniform buffers for each object
		for (auto &gameObject : gameObjects)
		{
			// Apply continuous rotation to the object based on frame time
			const float 			rotationSpeed		= 0.5f;				// Rotation speed in radians per second
			gameObject.rotation.y					+= rotationSpeed * deltaTime;	// Slow rotation around Y axis scaled by frame time
			
			// Get the model matrix for this object
			glm::mat4			model			= gameObject.getModelMatrix();
			
			// Create and update the UBO
			UniformBufferObject ubo
			{
				  .model					= model
				, .view						= view
				, .proj						= proj
			};
			
		// Copy the UBO data to the mapped memory
		memcpy(
			  gameObject.uniformBuffersMapped[frameIndex]
			, &ubo
			, sizeof(ubo)
		);
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
            // 	 while renderFinishedSemaphores is indexed by imageIndex
            auto 			        fenceResult				= device.waitForFences(
                                                                    *inFlightFences[frameIndex]
                                                                    , vk::True
                                                                    , UINT64_MAX
                                                                );
            if (fenceResult != vk::Result::eSuccess)
            {
                throw std::runtime_error("Failed to wait for fence!");
            }

            auto [
                  result
                , imageIndex
                ]           = swapChain.acquireNextImage(
			                                          UINT64_MAX
                                                    , *presentCompleteSemaphores[frameIndex]
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

		// Update uniform buffers for all objects
            updateUniformBuffers();

            // Only reset the fence if we are submitting work                    
            device.resetFences(*inFlightFences[frameIndex]);

            commandBuffers[frameIndex].reset();
            recordCommandBuffer(imageIndex);

            vk::PipelineStageFlags      waitDestinationStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);

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
//  Name:           checkValidationLayerSupport
//  Arguments:      
//  Returns:        bool
//  Calls:          
//  Called by:      
//  Description:    
// 
//******************************************************************************************

	[[nodiscard]] bool checkValidationLayerSupport() const
	{
		return (std::ranges::any_of(
					context.enumerateInstanceLayerProperties()
					, [](vk::LayerProperties const &lp)
					{
						return (
							strcmp("VK_LAYER_KHRONOS_validation"
							, lp.layerName) == 0
						);
					}
				)
			);
	}
	

//******************************************************************************************
// 
//  Name:           checkValidationLayerSupport
//  Arguments:      vk::DebugUtilsMessageSeverityFlagBitsEXT severity
//                  , vk::DebugUtilsMessageTypeFlagsEXT type
//                  , const vk::DebugUtilsMessengerCallbackDataEXT *pCallbackData
//                  , void *
//  Returns:        bool
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
		if (   severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError
			|| severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning)
		{
			std::cerr << "Validation layer: type " << to_string(type) << " msg: " << pCallbackData->pMessage<< std::endl;
		}
		
		return vk::False;
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
        VulkanApplication app;
        app.run();
    }
    catch (const std::exception &e)
    {
        LOGE("%s", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
#endif