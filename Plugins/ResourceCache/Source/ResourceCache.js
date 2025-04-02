// ResourceCache.js - Resource caching system for BabylonNative
// This file is loaded by the ResourceCache plugin to provide resource caching functionality

class ResourceCache {
    constructor() {
        this._resources = new Map();
        this._eventHandlers = new Map();
        this._scene = null; // Store scene reference
        this._pendingResources = null; // For resources loaded before scene is set
    }
    
    // Set the scene to be used by the ResourceCache
    setScene(scene) {
        this._scene = scene;
        
        // If we have pending resources that were attempted to be loaded before a scene was available,
        // load them now that we have a scene
        if (this._pendingResources) {
            console.log(`Loading ${this._pendingResources.length} pending resources now that scene is available`);
            const resources = this._pendingResources;
            this._pendingResources = null;
            this._loadResources(resources);
        }
        
        return this; // For method chaining
    }
    
    // Load resources from JSON string
    loadFromJSON(jsonString) {
        const resources = JSON.parse(jsonString).resources;
        
        // Store resource definitions
        resources.forEach(resource => {
            this._resources.set(resource.id, resource);
        });
        
        // Check if we have a scene
        if (!this._scene) {
            console.warn("No scene set. Resources will be queued until a scene is provided.");
            // Store resources for later loading
            this._pendingResources = resources;
            return;
        }
        
        // Load resources with the available scene
        this._loadResources(resources);
    }
    
    // Internal method to load resources using the current scene
    _loadResources(resources) {
        // Create an AssetManager for the current scene
        const assetsManager = new BABYLON.AssetsManager(this._scene);
        
        // Create tasks for each resource
        resources.forEach(resource => {
            this._createAssetTask(assetsManager, resource);
        });
        
        // Start loading
        assetsManager.load();
    }
    
    // Create appropriate asset task based on resource type
    _createAssetTask(assetsManager, resource) {
        let task;
        
        switch (resource.type) {
            case "texture":
                task = assetsManager.addTextureTask(
                    resource.id, 
                    resource.url
                );
                break;
            case "model":
                task = assetsManager.addMeshTask(
                    resource.id,
                    "",
                    resource.url.substring(0, resource.url.lastIndexOf("/")),
                    resource.url.substring(resource.url.lastIndexOf("/") + 1)
                );
                break;
            case "audio":
                task = assetsManager.addBinaryFileTask(
                    resource.id,
                    resource.url
                );
                
                task.onSuccess = (task) => {
                    // Create sound from binary data
                    const sound = new BABYLON.Sound(
                        resource.id,
                        task.data,
                        this._scene, // Use our stored scene reference
                        null,
                        { 
                            loop: resource.loop, 
                            autoplay: resource.autoplay 
                        }
                    );
                    
                    // Store on the scene for later reference
                    if (!this._scene._cachedResources) {
                        this._scene._cachedResources = new Map();
                    }
                    
                    this._scene._cachedResources.set(resource.id, sound);
                };
                break;
        }
        
        if (task) {
            // Add success handler for all task types
            const originalOnSuccess = task.onSuccess;
            
            task.onSuccess = (taskResult) => {
                // Call original success handler if it exists
                if (originalOnSuccess) {
                    originalOnSuccess(taskResult);
                }
                
                console.log(`Resource loaded: ${resource.id}`);
                
                // Store resource in scene
                this._storeResourceInScene(resource.id, taskResult);
                
                // Emit event
                this._emit("resourceLoaded", {
                    id: resource.id,
                    type: resource.type
                });
            };
            
            // Add error handler
            task.onError = (task, message) => {
                console.error(`Failed to load resource ${resource.id}: ${message}`);
                
                this._emit("resourceError", {
                    id: resource.id,
                    error: message
                });
            };
        }
    }
    
    // Update a resource
    updateResource(id, newUrl) {
        if (!this._resources.has(id)) {
            console.warn(`Resource ${id} not found`);
            return;
        }
        
        const resource = this._resources.get(id);
        resource.url = newUrl;
        
        // Check if we have a scene
        if (!this._scene) {
            console.warn("No scene set. Cannot update resource until a scene is provided.");
            return;
        }
        
        // Create a dedicated AssetsManager for this update
        const assetsManager = new BABYLON.AssetsManager(this._scene);
        
        this._createAssetTask(assetsManager, resource);
        
        // Load just this resource
        assetsManager.load();
    }
    
    // Event handling
    on(event, handler) {
        if (!this._eventHandlers.has(event)) {
            this._eventHandlers.set(event, []);
        }
        
        this._eventHandlers.get(event).push(handler);
    }
    
    _emit(event, data) {
        const handlers = this._eventHandlers.get(event) || [];
        handlers.forEach(handler => handler(data));
    }
    
    // Store resource in scene for easy access
    _storeResourceInScene(id, taskResult) {
        // Check if we have a scene
        if (!this._scene) {
            console.warn(`No scene set. Cannot store resource ${id}.`);
            return;
        }
        
        // Create the cache map if needed
        if (!this._scene._cachedResources) {
            this._scene._cachedResources = new Map();
        }
        
        // Store the actual resource based on task type
        let resource;
        
        if (taskResult.texture) {
            resource = taskResult.texture;
        } else if (taskResult.loadedMeshes) {
            resource = taskResult.loadedMeshes;
        } else {
            // For other types or direct resources
            resource = taskResult;
        }
        
        this._scene._cachedResources.set(id, resource);
    }
    
    // Get a resource by ID
    getResource(id) {
        // Check if we have a scene and cache
        if (!this._scene || !this._scene._cachedResources) {
            return null;
        }
        
        return this._scene._cachedResources.get(id);
    }
}

// Register singleton instance
BABYLON._resourceCache = new ResourceCache();

// Add helper function to access the ResourceCache
BABYLON.getResourceCache = function() {
    return BABYLON._resourceCache;
};
