#pragma once

#include <EngineRuntime.h>

void RegisterWindow(Engine::Windows::IWindow * window);
void UnregisterWindow(Engine::Windows::IWindow * window);
void DisplayWindow(Engine::Windows::IWindow * window);
void HideWindow(Engine::Windows::IWindow * window);

void RunDocumentationWindow(Engine::UI::InterfaceTemplate & interface);
void CloseDocumentationWindow(void);