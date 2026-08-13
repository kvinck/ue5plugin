/*
 * Copyright MediaZ Teknoloji A.S. All Rights Reserved.
 */

#pragma once
#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Nodos/AppAPI.h"
#include "AppEvents_generated.h"
#include <nosFlatBuffersCommon.h>
#include "NOSSceneTree.h"
#include "NOSClient.h"
#include "NOSViewportClient.h"
#include "NOSAssetManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogNOSSceneTreeManager, Log, All);

struct NOSPortal
{
	FGuid Id;
	FGuid SourceId;

	FString DisplayName;
	FString TypeName;
	FString CategoryName;
	nos::fb::ShowAs ShowAs;
	FString UniqueName;
};

//Accumulates the node updates produced during a single OnNOSLoadNodesOnPaths call so they can be
//flushed to Nodos as one BatchAppEvent instead of many individual SendPartialNodeUpdate messages.
//Builder holds the serialized data; Events holds one AppEvent offset (a PartialNodeUpdate) per node.
struct FNodeUpdateBatch
{
	flatbuffers::FlatBufferBuilder Builder;
	std::vector<flatbuffers::Offset<nos::app::AppEvent>> Events;
};

// A pin from a saved graph that has no object to bind to yet.
//
// Importing a node is a single pass over a snapshot of the world: OnNOSNodeImported
// builds the set of actors that exist at that instant, and every saved pin naming an
// actor outside it is skipped. Nothing ever revisits those pins, so a level streamed
// in after launch comes back with its nodes intact and none of its connections - the
// binding was never registered on this side at all.
//
// Holding the binding here instead lets it be resolved whenever its actor turns up,
// however much later that is. The same record is used to park a portal whose level is
// being streamed out, so an unload and a never-loaded level take the same path back.
struct FPendingPinBinding
{
	// The id the saved graph is wired to. For a portal this is the portal's own id,
	// which CreatePortal derives again from the source property's id - the two have to
	// agree or there is nothing on the Nodos side for the new pin to reattach to.
	FGuid PinId;
	FString ComponentName;
	FString PropertyPath;
	FString ContainerPath;
	FString DisplayName;
	FString FunctionName;
	FString FunctionPropertyName;
	nos::fb::ShowAs PinShowAs = nos::fb::ShowAs::PROPERTY;
	bool IsPortal = false;
	// Owned copies. The import's buffers are freed when it returns, and a parked
	// portal's property is destroyed with its level.
	TArray<uint8> Value;
	TArray<uint8> DefaultValue;
};

// A binding that found its property. Portal creation is held back until after the
// node carrying that property has been sent - a portal names its source pin, and
// Nodos has to have been told about the pin before something points at it.
struct FResolvedPinBinding
{
	TSharedPtr<NOSProperty> Property;
	nos::fb::ShowAs ShowAs = nos::fb::ShowAs::PROPERTY;
	bool bWantsPortal = false;
	bool bValueApplied = false;
};

//This class holds the list of all properties and pins 
class NOSSCENETREEMANAGER_API FNOSPropertyManager
{
public:
	FNOSPropertyManager(NOSSceneTree& sceneTree);

	TSharedPtr<NOSProperty> CreateProperty(UObject* container,
		FProperty* uproperty,
		FString parentCategory = FString(""),
		bool bAddToCache = true);

	void SetPropertyValue();
	bool CheckPinShowAs(nos::fb::CanShowAs CanShowAs, nos::fb::ShowAs ShowAs);
	void CreatePortal(FGuid PropertyId, nos::fb::ShowAs ShowAs);
	void CreatePortal(FProperty* uproperty, UObject* Container, nos::fb::ShowAs ShowAs);
	void ActorDeleted(FGuid DeletedActorId);
	flatbuffers::Offset<nos::fb::Pin> SerializePortal(flatbuffers::FlatBufferBuilder& fbb, NOSPortal const& Portal, NOSProperty* SourceProperty);
	void CreatePortalForTransformProperty(USceneComponent* RootComponent, const FName& Name);
	
	FNOSClient* NOSClient = nullptr;
	NOSSceneTree& SceneTree;

	TMap<FGuid, FGuid> PropertyToPortalPin;
	TMap<FGuid, NOSPortal> PortalPinsById;
	TMap<FGuid, TSharedPtr<NOSProperty>> PropertiesById;

	TMap<TPair<FProperty*, void*>, TSharedPtr<NOSProperty>> PropertiesByPropertyAndContainer;
	TMap<TPair<void*, UFunction*>, TSharedPtr<NOSFunction>> FunctionsByContainerAndUEFunction;
	void Reset(bool ResetPortals = true);

	void OnBeginFrame();
	void OnEndFrame();
};

struct SavedActorData
{
	TMap<FString, FString> Metadata;
	FName NodosUniqueName;
	FName NodosDisplayName;
};

class NOSSCENETREEMANAGER_API FNOSActorManager
{
public:
	FNOSActorManager(NOSSceneTree& SceneTree, TFunction<void(ActorNode*)> InOnActorAddedToSceneTree)
		: SceneTree(SceneTree), OnActorAddedToSceneTree(MoveTemp(InOnActorAddedToSceneTree))
	{
		NOSAssetManager = &FModuleManager::LoadModuleChecked<FNOSAssetManager>("NOSAssetManager");
		NOSClient = &FModuleManager::LoadModuleChecked<FNOSClient>("NOSClient");
		RegisterDelegates();
	};

	AActor* GetParentTransformActor();
	AActor* SpawnActor(FString SpawnTag, NOSSpawnActorParameters Params = {}, TMap<FString, FString> Metadata = {});
	AActor* SpawnUMGRenderManager(FString umgTag,UUserWidget* widget);
	void ClearActors();
	AActor* GetRealityLinoManager();
	
	void ReAddActorsToSceneTree();

	void RegisterDelegates();
	void PreSave(UWorld* World, FObjectPreSaveContext Context);
	void PostSave(UWorld* World, FObjectPostSaveContext Context);


	NOSActorReference ParentTransformActor;
	NOSActorReference RealityLinoManager;

	NOSSceneTree& SceneTree;
	class FNOSAssetManager* NOSAssetManager;
	class FNOSClient* NOSClient;
	TFunction<void(ActorNode*)> OnActorAddedToSceneTree;
	
	TSet<FGuid> ActorIds;
	TArray< TPair<NOSActorReference, SavedActorData> > Actors;
};


class ContextMenuActions
{
public:
	TArray<TPair<FString, std::function<void(class FNOSSceneTreeManager*, AActor*)>>>  ActorMenu;
	TArray<TPair<FString, Task>>  FunctionMenu;
	TArray<TPair<FString, std::function<void(class FNOSSceneTreeManager*, FGuid)>>>   PortalPropertyMenu;
	ContextMenuActions();
	std::vector<flatbuffers::Offset<nos::ContextMenuItem>> SerializeActorMenuItems(flatbuffers::FlatBufferBuilder& fbb);
	std::vector<flatbuffers::Offset<nos::ContextMenuItem>> SerializePortalPropertyMenuItems(flatbuffers::FlatBufferBuilder& fbb);
	void ExecuteActorAction(uint32 command, class FNOSSceneTreeManager* NOSSceneTreeManager, AActor* actor);
	void ExecutePortalPropertyAction(uint32 command, class FNOSSceneTreeManager* NOSSceneTreeManager, FGuid PortalId);
};


class NOSSCENETREEMANAGER_API FNOSSceneTreeManager : public IModuleInterface {

public:
	//Empty constructor
	FNOSSceneTreeManager();

	//Called on startup of the module on Unreal Engine start
	virtual void StartupModule() override;

	//Called on shutdown of the module on Unreal Engine exit
	virtual void ShutdownModule() override;

	bool Tick(float dt);
	bool CheckNewLevels(float dt);

	// The streaming levels of the current world, in a stable order.
	TArray<class ULevelStreaming*> GetStreamingLevels() const;
	// The pin that drives a given level. Derived from the level's package name, so it is
	// the same id every session and a graph saved today still names the same level.
	static FGuid GetLevelPinId(FName PackageName);
	// Whether the level is being asked for. Intent rather than the streaming state, so a
	// checkbox reads back the moment it is ticked rather than when the level finishes.
	bool IsStreamingLevelWanted(class ULevelStreaming* Level) const;
	// Brings a level in or takes it out.
	void SetStreamingLevelWanted(FName PackageName, bool bWanted);
	// Applies the checkbox states carried by a reloaded graph.
	void ApplySavedLevelPins(nos::fb::Node const& AppNode);
	// Acts on the levels a loaded graph asked for, once it is safe to.
	void TickPendingLevelRequests();

	// Records a saved pin that could not be bound, so it can be bound later.
	void StashPendingPinBinding(const struct PropUpdate& Update);
	// Builds an actor and everything under it without telling Nodos yet, collecting the
	// nodes that would have been sent. Deferring the send is the point: it is what lets
	// the saved values be applied before Nodos is told anything about these pins.
	void PopulateActorSubtreeDeferred(TreeNode* Node, TArray<FGuid>& OutNodesToSend);
	// Binds one saved pin now that its object exists. False leaves it stashed to try
	// again - an actor can be in the tree a frame before the property it names is.
	bool ResolvePendingPinBinding(AActor* Actor, FPendingPinBinding const& Binding, FResolvedPinBinding& OutResolved);
	// Resolves the bindings of every actor that turned up carrying them.
	void TickPendingPinBindings();

	// Queues every actor the last rescan found, to be populated over the coming
	// frames rather than when something first asks for one.
	void QueueBackgroundPopulate();
	// Adds an actor that appeared after the scan - spawned, attached or streamed
	// in - to the same queue.
	void QueueActorForBackgroundPopulate(ActorNode* Node);
	// Populates as many queued actors as fit in this frame's budget.
	void TickBackgroundPopulate();
	// Gathers the actors beneath a folder the scan produced.
	void CollectActorsToPopulate(TreeNode* Node);

	void OnBeginFrame();
	void OnEndFrame();

	//every function of this class runs in game thread
	void OnNOSNodeSelected(nos::fb::UUID const& nodeId);

	//called when connection is ended with Nodos
	void OnNOSConnectionClosed();

	//called when a pin value changed from Nodos
	void OnNOSPinValueChanged(nos::fb::UUID const& pinId, uint8_t const* data, size_t size, bool reset);

	//called when a pins show as changed
	void OnNOSPinShowAsChanged(nos::fb::UUID const& pinId, nos::fb::ShowAs newShowAs);

	//called when a function is called from Nodos
	void OnNOSFunctionCalled(nos::app::FunctionCall const& functionCall);

	//called when a context menu is requested on some node on Nodos
	void OnNOSContextMenuRequested(nos::app::AppContextMenuRequest const& request);

	//called when a action is selected from context menu
	void OnNOSContextMenuCommandFired(nos::app::AppContextMenuAction const& action);

	void OnNOSNodeRemoved();

	void OnNOSStateChanged_GRPCThread(nos::app::ExecutionState);
	
	void OnNOSLoadNodesOnPaths(const TArray<FString>& paths, FGuid requestId);
	//END OF Nodos DELEGATES
	 
	void PopulateAllChildsOfActor(FGuid ActorId, FNodeUpdateBatch* OptBatch = nullptr);

	void PopulateAllChildsOfSceneComponentNode(SceneComponentNode* SceneComponentNode, FNodeUpdateBatch* OptBatch = nullptr);

	void SendSyncSemaphores(bool RenewSemaphores);
	
	//Called when the level is initiated
	void OnPostWorldInit(UWorld* World, const UWorld::InitializationValues InitValues);

	//Called when the level destruction began
	void OnPreWorldFinishDestroy(UWorld* World);

	void OnLevelAddedToWorld(ULevel* Level, UWorld* World);

	void OnLevelRemovedFromWorld(ULevel* Level, UWorld* World);

	//delegate called when a property is changed from unreal engine editor
	//it updates thecorresponding property in Nodos
	void OnPropertyChanged(UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent);

	//Called when an actor is spawned into the world
	void OnActorSpawned(AActor* InActor);

	//Called when an actor is destroyed from the world
	void OnActorDestroyed(AActor* InActor);

	void OnActorAttached(AActor* Actor, const AActor* ParentActor);
	void OnActorDetached(AActor* Actor, const AActor* ParentActor);

	TSharedPtr<NOSFunction> FindFunctionByActorAndName(FGuid ActorId, const FString& FunctionName);

	//called when unreal engine node is imported from Nodos
	void OnNOSNodeImported(nos::fb::Node const& appNode);

	//Set a properties value
	void SetPropertyValue(FGuid pinId, void* newval, size_t size);

#ifdef VIEWPORT_TEXTURE
	//Set viewport texture pin's container to current viewport client's texture on play
	void ConnectViewportTexture();

	//Set viewport texture pin's container to null
	void DisconnectViewportTexture();
#endif

	//Rescans the current viewports world scene to get the current state of the scene outliner
	void RescanScene(bool reset = true);

	TSharedPtr<NOSFunction> AddFunctionToActorNode(ActorNode* actorNode, UFunction* UEFunction, UObject* Container);
	//Populates node with child actors/components, functions and properties
	bool PopulateNode(TreeNode* node);

	//Sends node updates to the Nodos. When OptBatch is given, the update is queued into it
	//instead of being sent immediately (see OnNOSLoadNodesOnPaths / FNodeUpdateBatch).
	void SendNodeUpdate(FGuid NodeId, bool bResetRootPins = true, bool filterPinsWhileSending = false, FNodeUpdateBatch* OptBatch = nullptr);

	//Serializes a node update into the given builder and returns its offset.
	//Returns a null offset when the node no longer exists in the scene tree.
	flatbuffers::Offset<nos::PartialNodeUpdate> BuildNodeUpdate(flatbuffers::FlatBufferBuilder& Builder, FGuid NodeId, bool bResetRootPins, bool filterPinsWhileSending);

	void SendEngineFunctionUpdate();

	//Sends pin value changed event to Nodos
	void SendPinValueChanged(FGuid propertyId, std::vector<uint8> data);

	//Sends pin updates to the root node 
	void SendPinUpdate();
	
	void RemovePortal(FGuid PortalId);
	
	//Sends pin to add to a node
	void SendPinAdded(FGuid NodeId, TSharedPtr<NOSProperty> const& nosprop);

	//Add to to-be-added actors list or send directly if always updating
	void SendActorAddedOnUpdate(AActor* actor, FString spawnTag = FString());

	//Adds the node to scene tree and sends it to Nodos
	void SendActorAdded(AActor* actor, FString spawnTag = FString());

	void SendActorDeletedOnUpdate(AActor* actor);

	//Deletes the node from scene tree and sends it to Nodos
	void SendActorDeleted(AActor* Actor);

	void SendParentChangedOnUpdate(FGuid Actor, FGuid ParentActor);

	void SendParentChanged(FGuid Actor, FGuid ParentActor);

	void SendActorNodeDeleted(ActorNode* node);
	
	void PopulateAllChildsOfActor(AActor* actor, FNodeUpdateBatch* OptBatch = nullptr);

	//This populates the node, its direct descendants, all of its child components and all of their children.
	void PopulateNodeAndDirectDescendants(TreeNode* Node, FNodeUpdateBatch* OptBatch = nullptr);

	void PopulateAndSendNode(TreeNode* Node, bool filterPinsWhileSending, FNodeUpdateBatch* OptBatch = nullptr);

	void ReloadCurrentMap();

	//Called when pie is started
	void HandleBeginPIE(bool bIsSimulating);

	//Called when pie is ending
	void HandleEndPIE(bool bIsSimulating);

	void HandleWorldChange();

	UObject* FindContainer(FGuid ActorId, FString ComponentName);

	void* FindContainerFromContainerPath(UObject* BaseContainer, FString ContainerPath, bool& IsResultUObject);

	// UObject* FNOSSceneTreeManager::FindObjectContainerFromContainerPath(UObject* BaseContainer, FString ContainerPath);
	//Remove properties of tree node from registered properties and pins
	void RemoveProperties(::TreeNode* Node,
	                      TSet<TSharedPtr<NOSProperty>>& PropertiesToRemove);

	void CheckPins(TSet<UObject*>& RemovedObjects,
		TSet<TSharedPtr<NOSProperty>>& PinsToRemove,
		TSet<TSharedPtr<NOSProperty>>& PropertiesToRemove);

	void Reset();

	void OnMapChange(uint32 MapFlags);
	void OnNewCurrentLevel();

	void AddCustomFunction(NOSCustomFunction* CustomFunction);
	
	void AddToBeAddedActors();
	void DeleteToBeDeletedActors();
	void ChangeParentActors();

	bool bTwoWayBindingEnabled = false;
	bool bTwoWayBindingStatusSent = false;
	void ToggleTwoWayBinding() { bTwoWayBindingEnabled = !bTwoWayBindingEnabled; }

	//the world we interested in
	static UWorld* daWorld;

	//all the properties registered 
	TMap<FGuid, TSharedPtr<NOSProperty>> RegisteredProperties;

	//all the properties registered mapped with property pointers
	TMap<FProperty*, TSharedPtr<NOSProperty>> PropertiesMap;

	//all the functions registered
	TMap<FGuid, TSharedPtr<NOSFunction>> RegisteredFunctions;

	//in/out pins of the Nodos node
	TMap<FGuid, TSharedPtr<NOSProperty>> Pins;

	//custom properties like viewport texture
	TMap<FGuid, TSharedPtr<NOSProperty>> CustomProperties;

#ifdef VIEWPORT_TEXTURE
	NOSProperty* ViewportTextureProperty;
#endif

	//custom functions like spawn actor
	TMap<FGuid, NOSCustomFunction*> CustomFunctions;

	//handles context menus and their actions
	friend class ContextMenuActions;
	class ContextMenuActions menuActions;

	//Scene tree holds the information to mimic the outliner in Nodos
	class NOSSceneTree SceneTree;
	
	//Class communicates with Nodos
	class FNOSClient* NOSClient;

	class FNOSAssetManager* NOSAssetManager;

	class FNOSViewportManager* NOSViewportManager;
	
	FNOSActorManager* NOSActorManager;

	FNOSPropertyManager NOSPropertyManager;

	bool bIsModuleFunctional = false;

	nos::app::ExecutionState ExecutionState = nos::app::ExecutionState::IDLE;

	bool ToggleExecutionStateToSynced = false;
	bool ShowHiddenActorsOnNodos = false;

	bool AlwaysUpdateOnActorSpawns = false;
	TArray<TWeakObjectPtr<AActor>> ActorsToBeAdded;
	TArray<FGuid> ActorsToBeDeleted;
	TMap<FGuid, FGuid> ActorsToBeParentChanged;
	TSet<TWeakObjectPtr<ULevel>> AlreadyLoadedStreamingLevels;

	TSet<FGuid> ActorsDeletedFromNodos;

	// Actors whose properties and functions have not been built yet.
	//
	// Held by id rather than by pointer: an actor can be destroyed between being
	// queued and being reached, and an id that no longer resolves is simply
	// skipped.
	TArray<FGuid> ActorsToBePopulated;
	// Membership index for the queue above. Actors are queued from the scan and
	// from every path that can add one afterwards, so duplicates are common and a
	// linear check would be quadratic over a scene's worth of them.
	TSet<FGuid> QueuedForPopulate;
	int32 BackgroundPopulateQueued = 0;
	double BackgroundPopulateStartedAt = 0.0;
	// Set once, at the first transition to synced. A latch, not a mirror of the
	// execution state: an idle switch later in the show does not put us back into
	// startup.
	//
	// Read only by TickPendingLevelRequests, which holds a loaded graph's level
	// changes until the app is up. Background population deliberately does NOT wait
	// on this - it did once, and holding the tree back until synced put it behind the
	// pass in which Nodos assigns every pin its ShowAs. Pins that miss that pass stay
	// PROPERTY forever, ProcessCopies only touches INPUT_PIN and OUTPUT_PIN, and every
	// texture input on the show rendered black for the life of the session. See the
	// comment above CVarBackgroundPopulate before wiring this to anything else.
	bool bHasGoneLive = false;

	// Saved pins waiting for their actor, keyed by the actor guid they name. Filled by
	// an import that could not resolve them, and by a level being streamed out.
	TMap<FGuid, TArray<FPendingPinBinding>> PendingPinBindings;
	// What the imported graph says each pin is worth, kept for the life of the import and
	// keyed by saved pin id. A level that streams out and back is expected to come back
	// reading what the graph holds rather than what the level was authored with, and the
	// property it names is destroyed in between - so the value has to be held here rather
	// than read back off the object at reload.
	TMap<FGuid, TArray<uint8>> SavedPinValues;
	// Actors that have appeared carrying pending bindings. Resolved from the tick
	// rather than on the spot, so the node is in the tree before anything looks for it.
	TArray<FGuid> PendingBindingActorsToResolve;
	// Set while a streaming level is being removed from the world. An actor destroyed
	// inside this window is expected back, so its portals are parked as orphans rather
	// than deleted - deleting them takes every connection Nodos holds with them.
	bool bUnloadingLevel = false;

	// Which level each checkbox on the Level Streaming node drives. Rebuilt every time
	// that node is serialized: the set of streaming levels belongs to the map, and there
	// is no map yet when the node is registered at startup.
	TMap<FGuid, FName> LevelPinToPackage;
	// What a loaded graph asked for, held until the app is up. Streaming a level in from
	// inside the import itself lands in the middle of everything else that starts at that
	// moment, and took Motion Design's broadcast down with it.
	TMap<FName, bool> PendingLevelRequests;
	double LevelRequestsQueuedAt = 0.0;

	static TSet<FGuid> PropertiesNeeded;

};

