#include "Volumes/SeinNavLinkProxy.h"

#include "Components/SceneComponent.h"

#if WITH_EDITOR
#include "Components/BillboardComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "DrawDebugHelpers.h"
#endif

ASeinNavLinkProxy::ASeinNavLinkProxy()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	StartAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("StartAnchor"));
	StartAnchor->SetupAttachment(Root);
	StartAnchor->SetRelativeLocation(FVector(-100.0f, 0.0f, 0.0f));

	EndAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("EndAnchor"));
	EndAnchor->SetupAttachment(Root);
	EndAnchor->SetRelativeLocation(FVector(100.0f, 0.0f, 0.0f));

#if WITH_EDITORONLY_DATA
	StartSprite = CreateDefaultSubobject<UBillboardComponent>(TEXT("StartSprite"));
	if (StartSprite)
	{
		StartSprite->SetupAttachment(StartAnchor);
		StartSprite->bIsEditorOnly = true;
		StartSprite->bHiddenInGame = true;
	}

	EndSprite = CreateDefaultSubobject<UBillboardComponent>(TEXT("EndSprite"));
	if (EndSprite)
	{
		EndSprite->SetupAttachment(EndAnchor);
		EndSprite->bIsEditorOnly = true;
		EndSprite->bHiddenInGame = true;
	}
#endif
}

FTransform ASeinNavLinkProxy::GetStartWorldTransform() const
{
	return StartAnchor ? StartAnchor->GetComponentTransform() : GetActorTransform();
}

FTransform ASeinNavLinkProxy::GetEndWorldTransform() const
{
	return EndAnchor ? EndAnchor->GetComponentTransform() : GetActorTransform();
}

void ASeinNavLinkProxy::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

#if WITH_EDITOR
	// Draw a persistent debug line between the endpoints in the editor for visibility.
	if (GIsEditor && !GetWorld()->IsGameWorld())
	{
		const FVector Start = GetStartWorldTransform().GetLocation();
		const FVector End = GetEndWorldTransform().GetLocation();
		const FColor Color = bBidirectional ? FColor(120, 200, 255) : FColor(255, 200, 120);
		DrawDebugLine(GetWorld(), Start, End, Color, /*bPersistent*/ false, /*LifeTime*/ -1.0f, /*Depth*/ 0, /*Thickness*/ 3.0f);
	}
#endif
}
