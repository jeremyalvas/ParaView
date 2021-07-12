/*=========================================================================

Program:   ParaView
Module:    TestCommandLineProcess.cxx

Copyright (c) Kitware, Inc.
All rights reserved.
See Copyright.txt or http://www.paraview.org/HTML/Copyright.html for details.

This software is distributed WITHOUT ANY WARRANTY; without even
the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "vtkInitializationHelper.h"
#include "vtkNew.h"
#include "vtkProcessModule.h"
#include "vtkSMParaViewPipelineController.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMProxy.h"
#include "vtkSMSession.h"
#include "vtkSMSessionProxyManager.h"
#include "vtkSmartPointer.h"
#include "vtkTestUtilities.h"

#include <string>

int TestCommandLineProcess(int argc, char* argv[])
{
  vtkInitializationHelper::SetApplicationName("TestCommandLineProcess");
  vtkInitializationHelper::SetOrganizationName("Humanity");
  vtkInitializationHelper::Initialize(argv[0], vtkProcessModule::PROCESS_CLIENT);

  vtkNew<vtkSMParaViewPipelineController> controller;
  vtkNew<vtkSMSession> session;

  // Register the session with the process module.
  vtkProcessModule::GetProcessModule()->RegisterSession(session.Get());

  // Initializes a session and setups all basic proxies that are needed for a
  // ParaView-like application.
  controller->InitializeSession(session.Get());

  // Setup a proxy to a command line process
  vtkSMSessionProxyManager* pxm = session->GetSessionProxyManager();
  vtkSmartPointer<vtkSMProxy> proxy;
  proxy.TakeReference(pxm->NewProxy("misc", "CommandLineProcess"));
  if (!proxy)
  {
    vtkGenericWarningMacro("Failed to create proxy: `misc,CommandLineProcess`. Aborting !!!");
    abort();
  }

  // Call a command line process
  vtkSMPropertyHelper(proxy->GetProperty("Command")).Set("echo Hello World");
  proxy->UpdateVTKObjects();
  proxy->InvokeCommand("Execute");
  auto* outProp = proxy->GetProperty("StdOut");
  proxy->UpdatePropertyInformation(outProp);
  std::string result = vtkSMPropertyHelper(outProp).GetAsString();

  // Check if output is good
  if (result != "Hello World")
  {
    vtkGenericWarningMacro(
      "Wrong output, command line failed. Expected: 'Hello World', received: '" << result << "'");
    abort();
  }

  // Unregistering pipeline proxies will also release any representations
  // created for these proxies.
  controller->UnRegisterProxy(proxy);

  vtkProcessModule::GetProcessModule()->UnRegisterSession(session.Get());
  vtkInitializationHelper::Finalize();
  return 0;
}
