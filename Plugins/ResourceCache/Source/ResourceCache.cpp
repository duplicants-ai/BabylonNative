#include <Babylon/Plugins/ResourceCache.h>
#include <Babylon/JsRuntime.h>
#include <Babylon/ScriptLoader.h>
#include <arcana/threading/task.h>
#include <napi/napi.h>
#include <memory>
#include <vector>
#include <mutex>
#include <atomic>
#include <unordered_map>
#include <string>
#include <utility> // For std::pair

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
        Babylon::JsRuntime& m_runtime;
        Napi::ObjectReference m_jsResourceCache;
        std::atomic<bool> m_jsReady{false};
        // Queue now stores pairs of (experienceId, jsonString)
        std::vector<std::pair<std::string, std::string>> m_pendingJsonQueue; 
        std::mutex m_pendingJsonMutex;

    public:
        ResourceCacheImpl(Babylon::JsRuntime& runtime);
        ~ResourceCacheImpl() = default;

        Babylon::JsRuntime& GetRuntime() { return m_runtime; }

        void SetJsObjectReady(Napi::Env env);
        void ProcessPendingJsonQueue(Napi::Env env);
        // Updated signatures to include experienceId
        void DispatchLoadResourcesFromJSON(Napi::Env env, const std::string& experienceId, const std::string& jsonString);
        void SetupJavaScriptImplementation(Napi::Env env);
        void LoadResourcesFromJSON(const std::string& experienceId, const std::string& jsonString); 
        void UpdateResource(const std::string& id, const std::string& newUrl);
        void SetScene(Napi::Value scene);
        Napi::Value GetJSObject(Napi::Env env) const;
    };

    // --- Start ResourceCacheImpl Member Function Definitions ---

    ResourceCacheImpl::ResourceCacheImpl(Babylon::JsRuntime& runtime)
        : m_runtime{runtime}
    {
        // Constructor
    }

    void ResourceCacheImpl::SetJsObjectReady(Napi::Env env)
    {
        try
        {
            auto global = env.Global();
            auto babylon = global.Get("BABYLON").As<Napi::Object>();
            if (babylon.Has("_resourceCache")) {
                m_jsResourceCache = Napi::Persistent(babylon.Get("_resourceCache").As<Napi::Object>());
                m_jsReady = true;
                ProcessPendingJsonQueue(env);
            } else {
                 Napi::Error::New(env, "SetJsObjectReady called but BABYLON._resourceCache not found.").ThrowAsJavaScriptException();
            }
        }
        catch (const Napi::Error& e) { Napi::Error::New(env, std::string("Napi Error in SetJsObjectReady: ") + e.what()).ThrowAsJavaScriptException(); }
        catch (const std::exception& e) { Napi::Error::New(env, std::string("Std Error in SetJsObjectReady: ") + e.what()).ThrowAsJavaScriptException(); }
    }

    void ResourceCacheImpl::ProcessPendingJsonQueue(Napi::Env env)
    {
        std::vector<std::pair<std::string, std::string>> pendingToProcess;
        {
            std::scoped_lock lock(m_pendingJsonMutex);
            pendingToProcess = std::move(m_pendingJsonQueue); // Use the renamed queue
            m_pendingJsonQueue.clear();
        }

        if (!pendingToProcess.empty())
        {
            // Dispatch each pending pair
            for (const auto& pair : pendingToProcess)
            {
                DispatchLoadResourcesFromJSON(env, pair.first, pair.second); // Pass both experienceId and jsonString
            }
        }
    }

    // Updated definition
    void ResourceCacheImpl::DispatchLoadResourcesFromJSON(Napi::Env env, const std::string& experienceId, const std::string& jsonString)
    {
        // Call the loadFromJSON method on our cached JS object
        // Pass experienceId as the third argument to the JS function (adjust JS accordingly)
        m_jsResourceCache.Value().As<Napi::Object>().Get("loadFromJSON")
            .As<Napi::Function>()
            .Call(
                m_jsResourceCache.Value(),
                {
                    Napi::String::New(env, jsonString),
                    Napi::String::New(env, experienceId) // Pass experienceId
                }
            );
    }

    void ResourceCacheImpl::SetupJavaScriptImplementation(Napi::Env env)
    {
        ENV_UNUSED(env)
        Babylon::ScriptLoader loader{m_runtime};
        loader.LoadScript("app:///Scripts/ResourceCache.js");
    }

    // Updated definition
    void ResourceCacheImpl::LoadResourcesFromJSON(const std::string& experienceId, const std::string& jsonString)
    {
        if (m_jsReady)
        {
            m_runtime.Dispatch([this, experienceId, jsonString](Napi::Env env) { // Capture experienceId
                DispatchLoadResourcesFromJSON(env, experienceId, jsonString);
            });
        }
        else
        {
            std::scoped_lock lock(m_pendingJsonMutex);
            m_pendingJsonQueue.emplace_back(experienceId, jsonString); // Store pair in queue
        }
    }

    void ResourceCacheImpl::UpdateResource(const std::string& id, const std::string& newUrl)
    {
        m_runtime.Dispatch([this, id, newUrl](Napi::Env env) {
            m_jsResourceCache.Value().As<Napi::Object>().Get("updateResource")
                .As<Napi::Function>()
                .Call(
                    m_jsResourceCache.Value(),
                    { Napi::String::New(env, id), Napi::String::New(env, newUrl) }
                );
        });
    }

    void ResourceCacheImpl::SetScene(Napi::Value scene)
    {
        m_runtime.Dispatch([this, scene](Napi::Env env) {
            ENV_UNUSED(env)
            m_jsResourceCache.Value().As<Napi::Object>().Get("setScene")
                .As<Napi::Function>()
                .Call( m_jsResourceCache.Value(), {scene} );
        });
    }

    Napi::Value ResourceCacheImpl::GetJSObject(Napi::Env env) const
    {
        ENV_UNUSED(env)
        if (!m_jsResourceCache.IsEmpty()) {
             return m_jsResourceCache.Value();
        }
        return env.Null();
    }

    // --- End ResourceCacheImpl Member Function Definitions ---


    // --- Start ResourceCache Public Interface Implementation ---

    ResourceCache::ResourceCache(Babylon::JsRuntime& runtime)
        : m_impl{ std::make_unique<ResourceCacheImpl>(runtime) }
    {
        std::scoped_lock lock(s_mapMutex);
        s_runtimeToImplMap[&runtime] = m_impl.get();
    }

    ResourceCache::~ResourceCache()
    {
        if (m_impl) {
             std::scoped_lock lock(s_mapMutex);
             JsRuntime& runtimeRef = m_impl->GetRuntime();
             s_runtimeToImplMap.erase(&runtimeRef);
        }
    }

    void ResourceCache::SetScene(Napi::Value scene)
    {
        m_impl->SetScene(scene);
    }

    // Updated definition
    void ResourceCache::LoadResourcesFromJSON(const std::string& experienceId, const std::string& jsonString)
    {
        m_impl->LoadResourcesFromJSON(experienceId, jsonString);
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
        } catch (const Napi::Error& e) { Napi::Error::New(env, std::string("Napi Error in SetJsReadyCallback: ") + e.what()).ThrowAsJavaScriptException(); }
        catch (const std::exception& e) { Napi::Error::New(env, std::string("Std Error in SetJsReadyCallback: ") + e.what()).ThrowAsJavaScriptException(); }
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
