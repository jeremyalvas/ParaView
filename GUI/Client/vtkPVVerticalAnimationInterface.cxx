/*=========================================================================

  Program:   ParaView
  Module:    vtkPVVerticalAnimationInterface.cxx

  Copyright (c) Kitware, Inc.
  All rights reserved.
  See Copyright.txt or http://www.paraview.org/HTML/Copyright.html for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "vtkPVVerticalAnimationInterface.h"

#include "vtkObjectFactory.h"
#include "vtkKWApplication.h"
#include "vtkKWFrame.h"
#include "vtkKWLabeledFrame.h"
#include "vtkKWLabel.h"
#include "vtkPVAnimationCue.h"
#include "vtkCommand.h"
#include "vtkKWParameterValueFunctionEditor.h"
#include "vtkPVTimeLine.h"
#include "vtkKWEvent.h"
#include "vtkPVKeyFrame.h"
#include "vtkKWMenuButton.h"
#include "vtkKWMenu.h"
#include "vtkKWPushButton.h"
#include "vtkPVRampKeyFrame.h"
#include "vtkPVBooleanKeyFrame.h"
#include "vtkPVExponentialKeyFrame.h"
#include "vtkPVSinusoidKeyFrame.h"

#include "vtkPVAnimationManager.h"
#include "vtkKWEntry.h"
#include "vtkKWScale.h"
#include "vtkKWCheckButton.h"

vtkStandardNewMacro(vtkPVVerticalAnimationInterface);
vtkCxxRevisionMacro(vtkPVVerticalAnimationInterface, "1.2");
vtkCxxSetObjectMacro(vtkPVVerticalAnimationInterface, ActiveKeyFrame, vtkPVKeyFrame);

#define VTK_PV_RAMP_INDEX 0
#define VTK_PV_RAMP_LABEL "Ramp"
#define VTK_PV_STEP_INDEX 1
#define VTK_PV_STEP_LABEL "Step"
#define VTK_PV_EXPONENTIAL_INDEX 2
#define VTK_PV_EXPONENTIAL_LABEL "Exponential"
#define VTK_PV_SINUSOID_INDEX 3
#define VTK_PV_SINUSOID_LABEL "Sinusoid"

//*****************************************************************************
class vtkPVVerticalAnimationInterfaceObserver : public vtkCommand
{
public:
  static vtkPVVerticalAnimationInterfaceObserver* New()
    {
    return new vtkPVVerticalAnimationInterfaceObserver;
    }
  void SetVerticalAnimationInterface(vtkPVVerticalAnimationInterface* ob)
    {
    this->VerticalAnimationInterface = ob;
    }
  virtual void Execute(vtkObject* ob, unsigned long event, void* calldata)
    {
    if (this->VerticalAnimationInterface)
      {
      this->VerticalAnimationInterface->ExecuteEvent(ob, event, calldata);
      }
    }
protected:
  vtkPVVerticalAnimationInterfaceObserver()
    {
    this->VerticalAnimationInterface = NULL;
    }
  vtkPVVerticalAnimationInterface* VerticalAnimationInterface;
};

//*****************************************************************************
//-----------------------------------------------------------------------------
vtkPVVerticalAnimationInterface::vtkPVVerticalAnimationInterface()
{
  this->TopFrame = vtkKWFrame::New();
  this->KeyFramePropertiesFrame = vtkKWLabeledFrame::New();
  this->ScenePropertiesFrame = vtkKWLabeledFrame::New();
  this->SelectKeyFrameLabel = vtkKWLabel::New();
  this->PropertiesFrame = vtkKWFrame::New();
  this->TypeFrame = vtkKWFrame::New();
  this->TypeImage = vtkKWPushButton::New();
  this->TypeLabel = vtkKWLabel::New();
  this->TypeMenuButton = vtkKWMenuButton::New();
  
  this->RecorderFrame = vtkKWLabeledFrame::New();
  this->InitStateButton = vtkKWPushButton::New();
  this->KeyFrameChangesButton = vtkKWPushButton::New();
  this->RecordAllLabel = vtkKWLabel::New();
  this->RecordAllButton = vtkKWCheckButton::New();

  this->SaveFrame = vtkKWLabeledFrame::New();
  this->CacheGeometryCheck = vtkKWCheckButton::New();
 
  this->AnimationCue = NULL;
  this->Observer = vtkPVVerticalAnimationInterfaceObserver::New();
  this->Observer->SetVerticalAnimationInterface(this);
  this->AnimationManager = NULL;
  this->ActiveKeyFrame = NULL;

  this->IndexLabel = vtkKWLabel::New();
  this->IndexScale = vtkKWScale::New();
  this->CacheGeometry = 1;
}

//-----------------------------------------------------------------------------
vtkPVVerticalAnimationInterface::~vtkPVVerticalAnimationInterface()
{
  this->Observer->Delete();
  this->TopFrame->Delete();
  this->KeyFramePropertiesFrame->Delete();
  this->ScenePropertiesFrame->Delete();
  this->SelectKeyFrameLabel->Delete();
  this->SetAnimationCue(NULL);
  this->PropertiesFrame->Delete();
  this->TypeFrame->Delete();
  this->TypeLabel->Delete();
  this->TypeImage->Delete();
  this->TypeMenuButton->Delete();
  this->SetAnimationManager(NULL);
  this->IndexLabel->Delete();
  this->IndexScale->Delete();
  
  this->RecorderFrame->Delete();
  this->InitStateButton->Delete();
  this->KeyFrameChangesButton->Delete();
  this->RecordAllLabel->Delete();
  this->RecordAllButton->Delete();

  this->SaveFrame->Delete();
  this->CacheGeometryCheck->Delete();
  
  this->SetActiveKeyFrame(NULL);
}

//-----------------------------------------------------------------------------
void vtkPVVerticalAnimationInterface::SetAnimationCue(vtkPVAnimationCue* cue)
{
  if (this->AnimationCue != cue)
    {
    if (this->AnimationCue)
      {
      this->RemoveObservers(this->AnimationCue);
      this->AnimationCue->UnRegister(this);
      }
    this->AnimationCue = cue;
    if (this->AnimationCue)
      {
      this->InitializeObservers(this->AnimationCue);
      this->AnimationCue->Register(this);
      }
    }
}

//-----------------------------------------------------------------------------
void vtkPVVerticalAnimationInterface::Create(vtkKWApplication* app,
  const char* args)
{
  if (!this->Superclass::Create(app, "frame", args ))
    {
    vtkErrorMacro("Failed creating widget " << this->GetClassName());
    return;
    }

  if (!this->AnimationManager)
    {
    vtkErrorMacro("AnimationManager must be set");
    return;
    }
  this->TopFrame->SetParent(this);
  this->TopFrame->ScrollableOn();
  this->TopFrame->Create(app, 0);
  this->Script("pack %s -side top -fill both -expand t -anchor center",
    this->TopFrame->GetWidgetName());

  this->ScenePropertiesFrame->SetParent(this->TopFrame->GetFrame());
  this->ScenePropertiesFrame->ShowHideFrameOn();
  this->ScenePropertiesFrame->Create(app, 0);
  this->ScenePropertiesFrame->SetLabel("Animation Control");
  this->Script("pack %s -side top -fill both -expand t -anchor center",
    this->ScenePropertiesFrame->GetWidgetName());
 
  // RECORDER FRAME
  this->RecorderFrame->SetParent(this->TopFrame->GetFrame());
  this->RecorderFrame->ShowHideFrameOn();
  this->RecorderFrame->Create(app, 0);
  this->RecorderFrame->SetLabel("Animation Recorder");
  this->Script("pack %s -side top -fill both -expand t -anchor center",
    this->RecorderFrame->GetWidgetName());

  this->RecordAllLabel->SetParent(this->RecorderFrame->GetFrame());
  this->RecordAllLabel->Create(app, 0);
  this->RecordAllLabel->SetLabel("Everything:");

  this->RecordAllButton->SetParent(this->RecorderFrame->GetFrame());
  this->RecordAllButton->Create(app, 0);
  this->RecordAllButton->SetState(this->AnimationManager->GetRecordAll());
  this->RecordAllButton->SetCommand(this, "RecordAllChangedCallback");
  this->RecordAllButton->SetBalloonHelpString("Specify if changes in all properties "
    "are to be recorded or only those in the selected property.");

  this->InitStateButton->SetParent(this->RecorderFrame->GetFrame());
  this->InitStateButton->Create(app, 0);
  this->InitStateButton->SetLabel("Init State");
  this->InitStateButton->SetCommand(this, "InitStateCallback");

  this->KeyFrameChangesButton->SetParent(this->RecorderFrame->GetFrame());
  this->KeyFrameChangesButton->Create(app, 0);
  this->KeyFrameChangesButton->SetLabel("Key frame changes");
  this->KeyFrameChangesButton->SetCommand(this, "KeyFrameChangesCallback");

  this->Script("grid propagate %s 1", this->RecorderFrame->GetFrame()->GetWidgetName());
  this->Script("grid %s %s - -sticky ew", this->RecordAllLabel->GetWidgetName(), 
    this->RecordAllButton->GetWidgetName());
  this->Script("grid %s - %s -sticky ew", this->InitStateButton->GetWidgetName(),
    this->KeyFrameChangesButton->GetWidgetName());
  this->Script("grid columnconfigure %s 0 -weight 0",
    this->RecorderFrame->GetFrame()->GetWidgetName());
  this->Script("grid columnconfigure %s 1 -weight 2",
    this->RecorderFrame->GetFrame()->GetWidgetName());
  this->Script("grid columnconfigure %s 2 -weight 2",
    this->RecorderFrame->GetFrame()->GetWidgetName());
  
  // KEYFRAME PROPERTIES FRAME
  this->KeyFramePropertiesFrame->SetParent(this->TopFrame->GetFrame());
  this->KeyFramePropertiesFrame->ShowHideFrameOn();
  this->KeyFramePropertiesFrame->Create(app, 0);
  this->KeyFramePropertiesFrame->SetLabel("Key Frame Properties");
  this->Script("pack %s -side top -fill both -expand t -anchor center",
    this->KeyFramePropertiesFrame->GetWidgetName());

  this->PropertiesFrame->SetParent(this->KeyFramePropertiesFrame->GetFrame());
  this->PropertiesFrame->Create(app, 0);

  this->IndexLabel->SetParent(this->PropertiesFrame->GetFrame());
  this->IndexLabel->Create(app, 0);
  this->IndexLabel->SetLabel("No:");
  
  this->IndexScale->SetParent(this->PropertiesFrame->GetFrame());
  this->IndexScale->Create(app,0);
  this->IndexScale->DisplayEntry();
  this->IndexScale->DisplayEntryAndLabelOnTopOff();
  this->IndexScale->SetResolution(1);
  this->IndexScale->SetCommand(this, "IndexChangedCallback");
  this->IndexScale->SetEntryCommand(this, "IndexChangedCallback");
  this->IndexScale->SetEndCommand(this, "IndexChangedCallback");
  this->IndexScale->SetBalloonHelpString("Select a key frame in the "
    "current sequence");
  
  this->TypeLabel->SetParent(this->PropertiesFrame->GetFrame());
  this->TypeLabel->Create(app, 0);
  this->TypeLabel->SetLabel("Out:");
 
  this->TypeImage->SetParent(this->PropertiesFrame->GetFrame());
  this->TypeImage->Create(app, "-relief flat");
  
  this->TypeMenuButton->SetParent(this->PropertiesFrame->GetFrame());
  this->TypeMenuButton->Create(app, "-image PVToolbarPullDownArrow -relief flat");
  this->TypeMenuButton->IndicatorOff();

  this->BuildTypeMenu();
    
  this->SelectKeyFrameLabel->SetParent(this->KeyFramePropertiesFrame->GetFrame());
  this->SelectKeyFrameLabel->SetLabel("Select a key frame to display it's "
    "properties.");
  this->SelectKeyFrameLabel->Create(app, 0);
  this->SelectKeyFrameLabel->AdjustWrapLengthToWidthOn();

  // SAVE FRAME
  this->SaveFrame->SetParent(this->TopFrame->GetFrame());
  this->SaveFrame->ShowHideFrameOn();
  this->SaveFrame->SetLabel("Cacheing");
  this->SaveFrame->Create(app, 0);
  this->Script("pack %s -side top -fill both -expand t -anchor center",
    this->SaveFrame->GetWidgetName());

  this->CacheGeometryCheck->SetParent(this->SaveFrame->GetFrame());
  this->CacheGeometryCheck->Create(app, 0);
  this->CacheGeometryCheck->SetText("Cache Geometry");
  this->CacheGeometryCheck->SetCommand(this, "CacheGeometryCheckCallback");
  this->CacheGeometryCheck->SetState(this->CacheGeometry);
  this->CacheGeometryCheck->SetBalloonHelpString(
    "Specify caching of geometry for the animation. Note that cache can be "
    "used only in Sequence mode.");
  this->Script("grid %s -sticky ew", this->CacheGeometryCheck->GetWidgetName());

  this->Update();
}

//-----------------------------------------------------------------------------
void vtkPVVerticalAnimationInterface::BuildTypeMenu()
{
  vtkKWMenu* menu = this->TypeMenuButton->GetMenu();
  char* var = menu->CreateRadioButtonVariable(this, "Radio");
  
  menu->AddRadioButton(VTK_PV_RAMP_INDEX, 
    VTK_PV_RAMP_LABEL, var, this, "SetKeyFrameType 0", 
    "Set the following Interpolator to Ramp.");
  menu->ConfigureItem(VTK_PV_RAMP_INDEX,"-image PVRamp"); 
  delete [] var;

  var = menu->CreateRadioButtonVariable(this, "Radio");
  menu->AddRadioButton(VTK_PV_STEP_INDEX, 
    VTK_PV_STEP_LABEL, var, this, "SetKeyFrameType 1",
    "Set the following Interpolator to Step.");
  menu->ConfigureItem(VTK_PV_STEP_INDEX,"-image PVStep");
  delete [] var;

  var = menu->CreateRadioButtonVariable(this, "Radio");
  menu->AddRadioButton(VTK_PV_EXPONENTIAL_INDEX, 
    VTK_PV_EXPONENTIAL_LABEL, var, this, "SetKeyFrameType 2",
    "Set the following Interpolator to Exponential.");
  menu->ConfigureItem(VTK_PV_EXPONENTIAL_INDEX,"-image PVExponential");
  delete [] var;

  var = menu->CreateRadioButtonVariable(this, "Radio");
  menu->AddRadioButton(VTK_PV_SINUSOID_INDEX,
    VTK_PV_SINUSOID_LABEL, var, this, "SetKeyFrameType 3",
    "Set the following Interpolator to Sinusoid.");
  menu->ConfigureItem(VTK_PV_SINUSOID_INDEX, "-image PVSinusoid");
  delete [] var;
}

//-----------------------------------------------------------------------------
void vtkPVVerticalAnimationInterface::SetKeyFrameType(int type)
{
  int id;
  if (!this->AnimationCue || !this->AnimationManager ||  
    this->AnimationCue->GetVirtual() ||
    (id = this->AnimationCue->GetTimeLine()->GetSelectedPoint())==-1)
    {
    vtkWarningMacro("This method should not have been called at all");
    return;
    }
   
  this->AddTraceEntry("$kw(%s) SetKeyFrameType %d", this->GetTclName(),
    type);

  this->AnimationManager->ReplaceKeyFrame(this->AnimationCue,
    type,  this->AnimationCue->GetKeyFrame(id));
  this->Update();
}

//-----------------------------------------------------------------------------
void vtkPVVerticalAnimationInterface::UpdateTypeImage(vtkPVKeyFrame* keyframe)
{
  if (vtkPVRampKeyFrame::SafeDownCast(keyframe))
    {
    this->TypeMenuButton->GetMenu()->CheckRadioButton(this, "Radio", 
      VTK_PV_RAMP_INDEX);
    this->TypeImage->ConfigureOptions("-image PVRamp");
    }
  else if (vtkPVBooleanKeyFrame::SafeDownCast(keyframe))
    {
    this->TypeMenuButton->GetMenu()->CheckRadioButton(this, "Radio", 
      VTK_PV_STEP_INDEX);
    this->TypeImage->ConfigureOptions("-image PVStep");
    }
  else if (vtkPVExponentialKeyFrame::SafeDownCast(keyframe))
    {
    this->TypeMenuButton->GetMenu()->CheckRadioButton(this, "Radio", 
      VTK_PV_EXPONENTIAL_INDEX);
    this->TypeImage->ConfigureOptions("-image PVExponential");
    }
  else if (vtkPVSinusoidKeyFrame::SafeDownCast(keyframe))
    {
    this->TypeMenuButton->GetMenu()->CheckRadioButton(this, "Radio",
      VTK_PV_SINUSOID_INDEX);
    this->TypeImage->ConfigureOptions("-image PVSinusoid");
    }
}

//-----------------------------------------------------------------------------
vtkKWFrame* vtkPVVerticalAnimationInterface::GetScenePropertiesFrame()
{
  if (!this->IsCreated())
    {
    vtkErrorMacro("Widget not created yet!");
    return NULL;
    }
  return this->ScenePropertiesFrame->GetFrame();
}

//-----------------------------------------------------------------------------
vtkKWFrame* vtkPVVerticalAnimationInterface::GetPropertiesFrame()
{
  if (!this->IsCreated())
    {
    vtkErrorMacro("Widget not created yet!");
    return NULL;
    }
  return this->PropertiesFrame;
}

//-----------------------------------------------------------------------------
void vtkPVVerticalAnimationInterface::CacheGeometryCheckCallback()
{
  this->SetCacheGeometry(this->CacheGeometryCheck->GetState());
}

//-----------------------------------------------------------------------------
void vtkPVVerticalAnimationInterface::SetCacheGeometry(int cache)
{
  if (cache == this->CacheGeometry)
    {
    return;
    }
  this->CacheGeometry = cache;
  this->CacheGeometryCheck->SetState(cache);
  this->AddTraceEntry("$kw(%s) SetCacheGeometry %d", this->GetTclName(), cache);
}

//-----------------------------------------------------------------------------
void vtkPVVerticalAnimationInterface::InitializeObservers(vtkPVAnimationCue* cue)
{
  cue->AddObserver(vtkKWParameterValueFunctionEditor::SelectionChangedEvent,
    this->Observer);
}

//-----------------------------------------------------------------------------
void vtkPVVerticalAnimationInterface::RemoveObservers(vtkPVAnimationCue* cue)
{
  cue->RemoveObservers(vtkKWParameterValueFunctionEditor::SelectionChangedEvent,
    this->Observer);
  cue->RemoveObservers(vtkKWEvent::FocusOutEvent, this->Observer);
}

//-----------------------------------------------------------------------------
void vtkPVVerticalAnimationInterface::Update()
{
  int id;
  if (this->AnimationCue == NULL || this->AnimationCue->GetVirtual() ||
    (id = this->AnimationCue->GetTimeLine()->GetSelectedPoint())==-1)
    {
    this->Script("pack forget %s", this->PropertiesFrame->GetWidgetName());
    this->Script("pack %s -side top -fill x -expand t -anchor center",
      this->SelectKeyFrameLabel->GetWidgetName());
    this->SetActiveKeyFrame(NULL);
    }
  else
    {
    this->IndexScale->SetRange(1, this->AnimationCue->GetNumberOfKeyFrames());
    this->IndexScale->SetValue(id+1);
    this->ShowKeyFrame(id);
    this->UpdateEnableState();
    this->Script("pack forget %s", this->SelectKeyFrameLabel->GetWidgetName());
    this->Script("pack %s -side top -fill x -expand t -anchor center",
      this->PropertiesFrame->GetWidgetName());
    }
}

//-----------------------------------------------------------------------------
void vtkPVVerticalAnimationInterface::ShowKeyFrame(int id)
{
  vtkPVKeyFrame* pvKeyFrame = this->AnimationCue->GetKeyFrame(id);
  this->SetActiveKeyFrame(pvKeyFrame);
  if (!pvKeyFrame)
    {
    vtkErrorMacro("Failed to get the keyframe");
    return;
    }
  //Lets try to determine time bounds if any for this keyframe.

  //Get rid of old times.
  pvKeyFrame->ClearTimeBounds();
  if (id > 0)
    {
    vtkPVKeyFrame* prev = this->AnimationCue->GetKeyFrame(id-1);
    if (prev)
      {
      pvKeyFrame->SetTimeMinimumBound(prev->GetKeyTime());
      }
    }

  if (id < this->AnimationCue->GetNumberOfKeyFrames()-1)
    {
    vtkPVKeyFrame* next = this->AnimationCue->GetKeyFrame(id+1);
    if (next)
      {
      pvKeyFrame->SetTimeMaximumBound(next->GetKeyTime());
      }
    }
  pvKeyFrame->PrepareForDisplay();
  this->UpdateTypeImage(pvKeyFrame);
  
  this->PropertiesFrame->GetFrame()->UnpackChildren();
  this->Script("grid %s %s - -sticky ew",
    this->IndexLabel->GetWidgetName(),
    this->IndexScale->GetWidgetName());
  
  this->Script("grid %s -columnspan 3 -sticky ew",
    pvKeyFrame->GetWidgetName());
  
  this->Script("grid %s %s %s -sticky w",
    this->TypeLabel->GetWidgetName(),
    this->TypeImage->GetWidgetName(),
    this->TypeMenuButton->GetWidgetName());

  this->Script("grid columnconfigure %s 2 -weight 2",
    this->PropertiesFrame->GetWidgetName());
}

//-----------------------------------------------------------------------------
void vtkPVVerticalAnimationInterface::ExecuteEvent(vtkObject*, unsigned long, void* )
{
  this->Update();
}

//-----------------------------------------------------------------------------
void vtkPVVerticalAnimationInterface::IndexChangedCallback()
{
  int val = this->IndexScale->GetEntry()->GetValueAsInt() - 1;
  this->SetKeyFrameIndex(val); 
}

//-----------------------------------------------------------------------------
void vtkPVVerticalAnimationInterface::SetKeyFrameIndex(int val)
{
  if (!this->AnimationCue || this->AnimationCue->GetVirtual())
    {
    return;
    } 
  if (val <0 || val >= this->AnimationCue->GetNumberOfKeyFrames())
    {
    return;
    }
  this->AnimationCue->GetTimeLine()->SelectPoint(val);
  this->IndexScale->SetValue(val+1);
  this->AddTraceEntry("$kw(%s) SetKeyFrameIndex %d", this->GetTclName(), val);
}

//-----------------------------------------------------------------------------
void vtkPVVerticalAnimationInterface::RecordAllChangedCallback()
{
  int state = this->RecordAllButton->GetState();
  this->AnimationManager->SetRecordAll(state);
}

//-----------------------------------------------------------------------------
void vtkPVVerticalAnimationInterface::InitStateCallback()
{
  this->AnimationManager->InitializeAnimatedPropertyStatus();
}

//-----------------------------------------------------------------------------
void vtkPVVerticalAnimationInterface::KeyFrameChangesCallback()
{
  this->AnimationManager->KeyFramePropertyChanges();
}

//-----------------------------------------------------------------------------
void vtkPVVerticalAnimationInterface::UpdateEnableState()
{
  this->Superclass::UpdateEnableState();
  this->PropagateEnableState(this->TypeMenuButton);
  this->PropagateEnableState(this->TypeImage);
  this->PropagateEnableState(this->IndexScale);
  this->PropagateEnableState(this->InitStateButton);
  this->PropagateEnableState(this->RecordAllLabel);
  this->PropagateEnableState(this->RecordAllButton);
  this->PropagateEnableState(this->KeyFrameChangesButton);
  this->PropagateEnableState(this->ScenePropertiesFrame);
  this->PropagateEnableState(this->KeyFramePropertiesFrame);
  this->PropagateEnableState(this->RecorderFrame);
  this->PropagateEnableState(this->SelectKeyFrameLabel);
  if (this->ActiveKeyFrame)
    {
    this->PropagateEnableState(this->ActiveKeyFrame);
    }
}

//-----------------------------------------------------------------------------
void vtkPVVerticalAnimationInterface::SaveState(ofstream* )
{
}

//-----------------------------------------------------------------------------
void vtkPVVerticalAnimationInterface::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "AnimationManager: " << this->AnimationManager << endl;
  os << indent << "AnimationCue: " << this->AnimationCue << endl;
  os << indent << "ActiveKeyFrame: " << this->ActiveKeyFrame << endl;
  os << indent << "CacheGeometry: " << this->CacheGeometry << endl;
}
