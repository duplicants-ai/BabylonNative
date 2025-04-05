#pragma once

#include <Babylon/Api.h>
#include <Babylon/JsRuntime.h>
#include <napi/env.h>
#include <string>
#include <memory>

namespace Babylon::Plugins::ResourceCache
{
    class ResourceCacheImpl;

    class ResourceCache
    {
    public:
        ResourceCache(Babylon::JsRuntime& runtime);
        ~ResourceCache();

        // Set the scene to be used by the ResourceCache
        void SetScene(Napi::Value scene);

        // Load resources from JSON for a specific experience
        void LoadResourcesFromJSON(const std::string& experienceId, const std::string& jsonString);
        
        // Update a specific resource
        void UpdateResource(const std::string& id, const std::string& newUrl);

        // Get the JavaScript object (for internal use)
        Napi::Value GetJSObject(Napi::Env env) const;

    private:
        std::unique_ptr<ResourceCacheImpl> m_impl;
    };

    // Initialize the plugin
    void BABYLON_API Initialize(Napi::Env env);
}
