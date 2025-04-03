#include <Babylon/Plugins/ResourceCache.h>
#include <Babylon/JsRuntime.h>
#include <Babylon/ScriptLoader.h>
#include <arcana/threading/task.h>
#include <napi/napi.h>
#include <memory>
#include <vector> // Added for queue
#include <mutex>  // Added for synchronization
#include <atomic> // Added for atomic flag
#include <unordered_map> // Added for map

#ifndef ENV_UNUSED
#define ENV_UNUSED(x) (void)(x);
#endif

namespace Babylon::Plugins::ResourceCache
{
// Forward declaration
class ResourceCacheImpl;

// Static map to associate JsRuntime with ResourceCacheImpl instance
static std::unordered_map<JsRuntime*, ResourceCacheImpl*> s_runtimeToImplMap;
static std::mutex s_mapMutex;

class ResourceCacheImpl
    {
    private:
        Babylon::JsRuntime& m_runtime; // Keep runtime reference here
        Napi::ObjectReference m_jsResourceCache;
        std::atomic<bool> m_jsReady{false}; // Flag to track JS readiness
        std::vector<std::string> m_pendingJson; // Queue for pending JSON loads
        std::mutex m_pendingJsonMutex; // Mutex to protect the queue

    public:
        ResourceCacheImpl(Babylon::JsRuntime& runtime);
        ~ResourceCacheImpl() = default;

        // Getter needed for destructor map cleanup
        Babylon::JsRuntime& GetRuntime() { return m_runtime; }

        void SetJsObjectReady(Napi::Env env);
        void ProcessPendingJsonQueue(Napi::Env env);
        void DispatchLoadResourcesFromJSON(Napi::Env env, const std::string& jsonString);
        void SetupJavaScriptImplementation(Napi::Env env);
        void LoadResourcesFromJSON(const std::string& jsonString);
        void UpdateResource(const std::string& id, const std::string& newUrl);
        void SetScene(Napi::Value scene);
        Napi::Value GetJSObject(Napi::Env env) const;
    }; // End ResourceCacheImpl class declaration

    // --- Start ResourceCacheImpl Member Function Definitions ---

    ResourceCacheImpl::ResourceCacheImpl(Babylon::JsRuntime& runtime)
        : m_runtime{runtime}
    {
        // Constructor body can be empty or contain initialization logic
    }

    void ResourceCacheImpl::SetJsObjectReady(Napi::Env env)
    {
        try
        {
            auto global = env.Global();
            auto babylon = global.Get("BABYLON").As<Napi::Object>();
            if (babylon.Has("_resourceCache")) {
                m_jsResourceCache = Napi::Persistent(babylon.Get("_resourceCache").As<Napi::Object>());
                m_jsReady = true; // Set flag AFTER successfully getting the object
                ProcessPendingJsonQueue(env); // Process queue now that JS is ready
            } else {
                 Napi::Error::New(env, "SetJsObjectReady called but BABYLON._resourceCache not found.").ThrowAsJavaScriptException();
            }
        }
        catch (const Napi::Error& e)
        {
             Napi::Error::New(env, std::string("Napi Error in SetJsObjectReady: ") + e.what()).ThrowAsJavaScriptException();
        }
        catch (const std::exception& e)
        {
             Napi::Error::New(env, std::string("Std Error in SetJsObjectReady: ") + e.what()).ThrowAsJavaScriptException();
        }
    }

    void ResourceCacheImpl::ProcessPendingJsonQueue(Napi::Env env)
    {
        std::vector<std::string> pendingToProcess;
        {
            std::scoped_lock lock(m_pendingJsonMutex);
            pendingToProcess = std::move(m_pendingJson);
            m_pendingJson.clear();
        }

        if (!pendingToProcess.empty())
        {
            for (const auto& jsonString : pendingToProcess)
            {
                DispatchLoadResourcesFromJSON(env, jsonString);
            }
        }
    }

    void ResourceCacheImpl::DispatchLoadResourcesFromJSON(Napi::Env env, const std::string& jsonString)
    {
        // Call the loadFromJSON method on our cached JS object
        m_jsResourceCache.Value().As<Napi::Object>().Get("loadFromJSON")
            .As<Napi::Function>()
            .Call(
                m_jsResourceCache.Value(),
                {Napi::String::New(env, jsonString)}
            );
    }

    void ResourceCacheImpl::SetupJavaScriptImplementation(Napi::Env env)
    {
        // Load the ResourceCache JavaScript implementation
        ENV_UNUSED(env)
        Babylon::ScriptLoader loader{m_runtime};
        loader.LoadScript("app:///Scripts/ResourceCache.js");
    }

    void ResourceCacheImpl::LoadResourcesFromJSON(const std::string& jsonString)
    {
        if (m_jsReady)
        {
            // JS is ready, dispatch immediately
            m_runtime.Dispatch([this, jsonString](Napi::Env env) {
                DispatchLoadResourcesFromJSON(env, jsonString);
            });
        }
        else
        {
            // JS not ready yet, queue the JSON string
            std::scoped_lock lock(m_pendingJsonMutex);
            m_pendingJson.push_back(jsonString);
        }
    }

    void ResourceCacheImpl::UpdateResource(const std::string& id, const std::string& newUrl)
    {
        m_runtime.Dispatch([this, id, newUrl](Napi::Env env) {
            // Call the updateResource method on our cached JS object
            m_jsResourceCache.Value().As<Napi::Object>().Get("updateResource")
                .As<Napi::Function>()
                .Call(
                    m_jsResourceCache.Value(),
                    {
                        Napi::String::New(env, id),
                        Napi::String::New(env, newUrl)
                    }
                );
        });
    }

    void ResourceCacheImpl::SetScene(Napi::Value scene)
    {
        m_runtime.Dispatch([this, scene](Napi::Env env) {
            ENV_UNUSED(env)
            // Call the setScene method on our cached JS object
            m_jsResourceCache.Value().As<Napi::Object>().Get("setScene")
                .As<Napi::Function>()
                .Call(
                    m_jsResourceCache.Value(), {scene}
                );
        });
    }

    Napi::Value ResourceCacheImpl::GetJSObject(Napi::Env env) const
    {
        ENV_UNUSED(env)
        // Ensure the JS object reference is valid before returning its value
        if (!m_jsResourceCache.IsEmpty()) {
             return m_jsResourceCache.Value();
        }
        return env.Null(); // Return null if the JS object isn't ready/valid
    }

    // --- End ResourceCacheImpl Member Function Definitions ---


    // --- Start ResourceCache Public Interface Implementation ---

    ResourceCache::ResourceCache(Babylon::JsRuntime& runtime)
        : m_impl{ std::make_unique<ResourceCacheImpl>(runtime) } // Only initialize m_impl
    {
        // Register instance in the map using the passed runtime parameter
        std::scoped_lock lock(s_mapMutex);
        s_runtimeToImplMap[&runtime] = m_impl.get(); // Use 'runtime' parameter as key
    }

    ResourceCache::~ResourceCache()
    {
        // Unregister instance from the map using the runtime stored in m_impl
        // Note: This assumes m_impl is still valid. If m_impl could be null here,
        // add a check.
        if (m_impl) {
             std::scoped_lock lock(s_mapMutex);
             // Get the runtime reference from the impl before erasing
             JsRuntime& runtimeRef = m_impl->GetRuntime();
             s_runtimeToImplMap.erase(&runtimeRef);
        }
        // Default unique_ptr destructor handles m_impl cleanup
    }

    void ResourceCache::SetScene(Napi::Value scene)
    {
        m_impl->SetScene(scene);
    }

    void ResourceCache::LoadResourcesFromJSON(const std::string& jsonString)
    {
        m_impl->LoadResourcesFromJSON(jsonString);
    }

    void ResourceCache::UpdateResource(const std::string& id, const std::string& newUrl)
    {
        m_impl->UpdateResource(id, newUrl);
    }

    Napi::Value ResourceCache::GetJSObject(Napi::Env env) const
    {
        return m_impl->GetJSObject(env);
    }

    // --- End ResourceCache Public Interface Implementation ---


    // --- Start Napi Initialization ---

    Napi::Value SetJsReadyCallback(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        try {
            JsRuntime& runtime = Babylon::JsRuntime::GetFromJavaScript(env);
            ResourceCacheImpl* implInstance = nullptr;
            {
                std::scoped_lock lock(s_mapMutex);
                auto it = s_runtimeToImplMap.find(&runtime);
                if (it != s_runtimeToImplMap.end()) {
                    implInstance = it->second;
                }
            }

            if (implInstance) {
                implInstance->SetJsObjectReady(env);
            } else {
                 Napi::Error::New(env, "__ResourceCacheSetJsReady: Could not find ResourceCacheImpl instance for current JsRuntime.").ThrowAsJavaScriptException();
            }
        } catch (const Napi::Error& e) {
             Napi::Error::New(env, std::string("Napi Error in SetJsReadyCallback: ") + e.what()).ThrowAsJavaScriptException();
        } catch (const std::exception& e) {
             Napi::Error::New(env, std::string("Std Error in SetJsReadyCallback: ") + e.what()).ThrowAsJavaScriptException();
        }
        return env.Undefined();
    }

    void Initialize(Napi::Env env)
    {
        Napi::Function initFunction = Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
            auto env = info.Env();
            auto global = env.Global();
            auto babylon = global.Get("BABYLON").As<Napi::Object>();
            
            babylon.Set("getResourceCache", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                auto env = info.Env();
                auto global = env.Global();
                auto babylon = global.Get("BABYLON").As<Napi::Object>();
                
                if (babylon.Has("_resourceCache")) {
                    return babylon.Get("_resourceCache");
                }
                
                return env.Null();
            }));
            
            return env.Undefined();
        });
        
        auto global = env.Global();
        global.Set("__ResourceCacheInitialize", initFunction);
        global.Set("__ResourceCacheSetJsReady", Napi::Function::New(env, SetJsReadyCallback));
    }

    // --- End Napi Initialization ---

} // namespace Babylon::Plugins::ResourceCache
