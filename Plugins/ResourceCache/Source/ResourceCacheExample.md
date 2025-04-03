# ResourceCache Plugin Usage Examples

This document provides examples of how to use the ResourceCache plugin in both C++ and JavaScript.

## C++ Usage Example

```cpp
// Include the ResourceCache header
#include <Babylon/Plugins/ResourceCache.h>

// In your application initialization
void MyApp::InitializeResourceCache()
{
    // Create the resource cache instance
    m_resourceCache = std::make_unique<Babylon::Plugins::ResourceCache::ResourceCache>(m_runtime);
    
    // Set the scene for the ResourceCache to use
    // This should be done after a scene is created in JavaScript
    m_runtime.Dispatch([this](Napi::Env env) {
        // Get a reference to the active scene
        auto scene = GetActiveScene(env); // Your implementation to get the scene
        
        // Set the scene in the ResourceCache
        m_resourceCache->SetScene(scene);
    });
    
    // JSON string defining resources to cache
    std::string resourcesJson = R"({
        "resources": [
            {
                "id": "texture1",
                "type": "texture",
                "url": "assets/textures/grass.png"
            },
            {
                "id": "model1",
                "type": "model",
                "url": "assets/models/character.glb"
            },
            {
                "id": "sound1",
                "type": "audio",
                "url": "assets/audio/background.mp3"
            }
        ]
    })";
    
    // Load resources from JSON (resources will be queued until a scene is set)
    m_resourceCache->LoadResourcesFromJSON(resourcesJson);
}

// Later, to update a resource (e.g., for hot-reloading)
void MyApp::UpdateTexture(const std::string& textureId, const std::string& newUrl)
{
    m_resourceCache->UpdateResource(textureId, "assets/textures/updated_grass.png");
}
```

## JavaScript Usage Example

```javascript
// Create a scene
const scene = new BABYLON.Scene(engine);

// Get the resource cache
const resourceCache = BABYLON.getResourceCache();

// Set the scene for the ResourceCache to use
resourceCache.setScene(scene);

// Use cached resources
const material = new BABYLON.StandardMaterial("material", scene);
const texture = resourceCache.getResource("texture1");
material.diffuseTexture = texture;

// Listen for resource updates
resourceCache.on("resourceLoaded", (data) => {
    console.log(`Resource loaded: ${data.id}`);
    
    // You can update materials or meshes when resources are loaded/updated
    if (data.id === "texture1") {
        const updatedTexture = resourceCache.getResource("texture1");
        material.diffuseTexture = updatedTexture;
    }
});

// You can also update resources from JavaScript
resourceCache.updateResource("texture1", "assets/textures/new_grass.png");
```

## JSON Format

The JSON format for defining resources is:

```json
{
  "resources": [
    {
      "id": "texture1",       // Unique identifier for the resource
      "type": "texture",      // Resource type: "texture", "model", or "audio"
      "url": "path/to/file"   // URL or file path to the resource
    }
  ]
}
```

## Resource Types

The following resource types are supported:

1. **texture** - Image files (png, jpg, etc.)
2. **model** - 3D models (glb, gltf, etc.)
3. **audio** - Sound files (mp3, wav, etc.)

## Notes

- The ResourceCache plugin integrates with Babylon.js's AssetManager for resource loading.
- Resources are automatically cached and made available to the scene.
- The plugin requires a scene to be loaded before resources can be loaded.
