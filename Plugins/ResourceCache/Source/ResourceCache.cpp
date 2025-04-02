#include <Babylon/Plugins/ResourceCache.h>
#include <Babylon/JsRuntime.h>
#include <Babylon/ScriptLoader.h>
#include <arcana/threading/task.h>
#include <napi/napi.h>
#include <memory>

#ifndef ENV_UNUSED
#define ENV_UNUSED(x) (void)(x);
#endif

namespace Babylon::Plugins::ResourceCache
{
class ResourceCacheImpl
    {
    private:
        Babylon::JsRuntime& m_runtime;
        Napi::ObjectReference m_jsResourceCache;

    public:
        ResourceCacheImpl(Babylon::JsRuntime& runtime)
            : m_runtime{runtime}
        {
            // Initialize the JavaScript ResourceCache when this object is created
            m_runtime.Dispatch([this](Napi::Env env) {
                // Check if ResourceCache is already initialized
                auto global = env.Global();
                auto babylon = global.Get("BABYLON").As<Napi::Object>();
                
                if (!babylon.Has("_resourceCache")) {
                    // Setup the ResourceCache JavaScript implementation
                    SetupJavaScriptImplementation(env);
                }
                
                // Store a reference to the JavaScript ResourceCache
                m_jsResourceCache = Napi::Persistent(babylon.Get("_resourceCache").As<Napi::Object>());
            });
        }
        
        // Sets up the JavaScript ResourceCache implementation
        void SetupJavaScriptImplementation(Napi::Env env)
        {
            // Load the ResourceCache JavaScript implementation
            ENV_UNUSED(env)
            Babylon::ScriptLoader loader{m_runtime};
            loader.LoadScript("app:///Scripts/ResourceCache.js");
        }
        
        // Load resources from JSON
        void LoadResourcesFromJSON(const std::string& jsonString)
        {
            m_runtime.Dispatch([this, jsonString](Napi::Env env) {
                // Call the loadFromJSON method on our cached JS object
                m_jsResourceCache.Value().As<Napi::Object>().Get("loadFromJSON")
                    .As<Napi::Function>()
                    .Call(
                        m_jsResourceCache.Value(),
                        {Napi::String::New(env, jsonString)}
                    );
            });
        }
        
        // Update a resource
        void UpdateResource(const std::string& id, const std::string& newUrl)
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
        
        // Set the scene to be used by the ResourceCache
        void SetScene(Napi::Value scene)
        {
            // Get a persistent reference to the scene
            //auto sceneRef = Napi::Persistent(scene);
            
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
        
        // Get the JavaScript object
        Napi::Value GetJSObject(Napi::Env env) const
        {
            ENV_UNUSED(env)
            return m_jsResourceCache.Value();
        }
    };

    // ResourceCache implementation
    ResourceCache::ResourceCache(Babylon::JsRuntime& runtime)
        : m_impl{std::make_unique<ResourceCacheImpl>(runtime)}
    {
    }

    ResourceCache::~ResourceCache() = default;

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

    // Initialize function for the plugin
    void Initialize(Napi::Env env)
    {
        auto global = env.Global();
        auto babylon = global.Get("BABYLON").As<Napi::Object>();
        
        // Add helper function to access the ResourceCache
        babylon.Set("getResourceCache", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
            auto env = info.Env();
            auto global = env.Global();
            auto babylon = global.Get("BABYLON").As<Napi::Object>();
            
            return babylon.Get("_resourceCache");
        }));
    }
}
