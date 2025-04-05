// ResourceCache.js - Resource caching system for BabylonNative
// This file is loaded by the ResourceCache plugin to provide resource caching functionality

class ResourceCache {
    constructor() {
        this._resources = new Map();
        this._eventHandlers = new Map();
        this._scene = null;
        this._pendingLoads = {}; // Stores { experienceId: resources[] } for loads attempted before scene was set
        this._loadingManifests = {}; // Track loading progress per experienceId
    }
    
    // Set the scene to be used by the ResourceCache
    setScene(scene) {
        if (this._scene === scene) {
            return this; // No change
        }
        console.log("ResourceCache: Scene set.");
        this._scene = scene;
        
        // Process any pending loads that were waiting for a scene
        const pendingExperienceIds = Object.keys(this._pendingLoads);
        if (pendingExperienceIds.length > 0) {
            console.log(`ResourceCache: Processing ${pendingExperienceIds.length} pending manifest loads now that scene is available.`);
            pendingExperienceIds.forEach(experienceId => {
                const resources = this._pendingLoads[experienceId];
                if (resources) {
                     console.log(`ResourceCache: Loading ${resources.length} pending resources for experience ${experienceId}`);
                    this._loadResources(resources, experienceId);
                }
            });
            // Clear the pending queue
            this._pendingLoads = {};
        }
        
        return this; // For method chaining
    }
    
    // Load resources from JSON string for a specific experience
    loadFromJSON(jsonString, experienceId) { // Added experienceId parameter
        if (!experienceId) {
            console.error("ResourceCache.loadFromJSON called without an experienceId.");
            return;
        }
        console.log(`ResourceCache: Starting loadFromJSON for experience: ${experienceId}`);

        const resources = JSON.parse(jsonString).resources;
        if (!resources || resources.length === 0) {
             console.log(`ResourceCache: No resources found in JSON for experience: ${experienceId}. Reporting completion immediately.`);
             // If no resources, report completion immediately
             this._checkManifestLoadComplete(experienceId, 0); 
             return;
        }
        
        // Store resource definitions
        resources.forEach(resource => {
            this._resources.set(resource.id, resource);
        });
        
        // Initialize tracking for this manifest load
        this._loadingManifests[experienceId] = {
            total: resources.length,
            loaded: 0,
            failed: 0
        };

        // Check if we have a scene
        if (!this._scene) {
            console.warn(`ResourceCache: No scene set for experience ${experienceId}. Resources will be queued until a scene is provided.`);
            // Store resources associated with the experienceId for later loading
            this._pendingLoads[experienceId] = resources; 
            return;
        }
        
        // Load resources with the available scene
        this._loadResources(resources, experienceId); // Pass experienceId
    }
    
    // Internal method to load resources using the current scene
    _loadResources(resources, experienceId) { // Added experienceId
        // Create an AssetManager for the current scene
        const assetsManager = new BABYLON.AssetsManager(this._scene);
        
        // Create tasks for each resource
        resources.forEach(resource => {
            this._createAssetTask(assetsManager, resource, experienceId); // Pass experienceId
        });
        
        // Start loading
        assetsManager.load();
    }
    
    // Create appropriate asset task based on resource type
    _createAssetTask(assetsManager, resource, experienceId) { // Added experienceId
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
                    type: resource.type,
                    experienceId: experienceId // Include experienceId in event data
                });
                // Update manifest loading progress
                this._updateManifestProgress(experienceId, true);
            };
            
            // Add error handler
            task.onError = (task, message) => {
                console.error(`Failed to load resource ${resource.id} for experience ${experienceId}: ${message}`);
                
                this._emit("resourceError", {
                    id: resource.id,
                    error: message,
                    experienceId: experienceId // Include experienceId in event data
                });
                 // Update manifest loading progress
                this._updateManifestProgress(experienceId, false);
            };
        } else {
             // If no task was created (e.g., unknown type), count it as failed for manifest tracking
             console.warn(`ResourceCache: Could not create task for resource ${resource.id} (type: ${resource.type}) in experience ${experienceId}. Counting as failed.`);
             this._updateManifestProgress(experienceId, false);
        }
    }

    // Track manifest loading progress and emit completion event
    _updateManifestProgress(experienceId, success) {
        const manifest = this._loadingManifests[experienceId];
        if (!manifest) {
            console.warn(`ResourceCache: Received progress update for unknown manifest: ${experienceId}`);
            return;
        }

        if (success) {
            manifest.loaded++;
        } else {
            manifest.failed++;
        }

        // Check if all resources for this manifest are accounted for
        if (manifest.loaded + manifest.failed === manifest.total) {
            this._checkManifestLoadComplete(experienceId, manifest.failed);
        }
    }

    // Check and emit manifest load completion
    _checkManifestLoadComplete(experienceId, failureCount) {
         const manifest = this._loadingManifests[experienceId];
         if (!manifest) return; // Should not happen if called correctly

         const overallSuccess = failureCount === 0;
         console.log(`ResourceCache: Manifest load complete for experience ${experienceId}. Success: ${overallSuccess} (${manifest.loaded}/${manifest.total} loaded, ${failureCount} failed)`);
            
         this._emit("manifestLoadComplete", {
             experienceId: experienceId,
             success: overallSuccess
         });

         // Clean up tracking for this manifest
         delete this._loadingManifests[experienceId];
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

// Initialize and register the ResourceCache
(function() {
    // Create the ResourceCache instance 
    var resourceCache = new ResourceCache();

    if (typeof BABYLON !== 'undefined') {
        // BABYLON already exists, initialize immediately
        BABYLON._resourceCache = resourceCache;
        
        // If the helper function initialization exists, call it
        if (typeof __ResourceCacheInitialize === 'function') {
            __ResourceCacheInitialize();
        }
    } else {
        // BABYLON doesn't exist yet, set up a callback for when it's created
        var originalBABYLON = null;
        Object.defineProperty(window, 'BABYLON', {
            get: function() { 
                return originalBABYLON; 
            },
            set: function(newValue) {
                originalBABYLON = newValue;
                
                // When BABYLON is set, initialize ResourceCache
                if (newValue) {
                    // Set the ResourceCache instance
                    newValue._resourceCache = resourceCache;
                    
                    // Call the initialization function to set up getResourceCache
                    if (typeof __ResourceCacheInitialize === 'function') {
                        __ResourceCacheInitialize();
                    }
                }
            },
            configurable: true
        });
    }

    // Signal C++ that the JS object is now ready
    if (typeof __ResourceCacheSetJsReady === 'function') {
        console.log("ResourceCache.js: Signaling C++ that JS object is ready.");
        __ResourceCacheSetJsReady(); 
    } else {
        console.error("ResourceCache.js: Cannot signal C++ readiness, __ResourceCacheSetJsReady not found.");
    }
})();
