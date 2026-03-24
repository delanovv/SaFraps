#include "main.h"
#include "VideoWriter.h"
#include "notify/imgui_notify.h"

// ============================================================
//  Globals
// ============================================================

static bool          g_initialized = false;
static bool          g_windowOpen = false;
static bool          g_wasOpen = false;
static bool          g_recording = false;

static VideoWriter         g_writer;
static VideoWriter::Config g_config;

// UI state
static char   g_pathBuf[MAX_PATH] = {};
static bool   g_pathInit = false;

static const char* k_presets[] = { "p1", "p2", "p3", "p4", "p5", "p6", "p7" };
static int         g_presetIdx = 3;

// ============================================================
//  Helpers
// ============================================================

static void ApplyTheme()
{
    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowRounding = 12.f;
    s.FrameRounding = 6.f;
    s.ChildRounding = 8.f;
    s.PopupRounding = 8.f;
    s.ScrollbarRounding = 6.f;
    s.GrabRounding = 6.f;
    s.TabRounding = 6.f;

    s.WindowPadding = { 20.f, 18.f };
    s.FramePadding = { 12.f, 7.f };
    s.ItemSpacing = { 10.f, 8.f };
    s.ItemInnerSpacing = { 8.f,  6.f };
    s.ScrollbarSize = 10.f;
    s.GrabMinSize = 10.f;
    s.WindowBorderSize = 1.f;
    s.FrameBorderSize = 0.f;

    ImVec4* c = s.Colors;

    // Base palette — dark charcoal + electric indigo accent
    c[ImGuiCol_WindowBg] = { 0.09f, 0.10f, 0.12f, 1.00f };
    c[ImGuiCol_ChildBg] = { 0.11f, 0.13f, 0.16f, 1.00f };
    c[ImGuiCol_PopupBg] = { 0.09f, 0.10f, 0.12f, 1.00f };
    c[ImGuiCol_Border] = { 1.00f, 1.00f, 1.00f, 0.06f };
    c[ImGuiCol_BorderShadow] = { 0.00f, 0.00f, 0.00f, 0.00f };

    // Frames / inputs
    c[ImGuiCol_FrameBg] = { 0.15f, 0.17f, 0.21f, 1.00f };
    c[ImGuiCol_FrameBgHovered] = { 0.18f, 0.21f, 0.26f, 1.00f };
    c[ImGuiCol_FrameBgActive] = { 0.20f, 0.23f, 0.30f, 1.00f };

    // Title bar (unused since NoTitleBar, kept for completeness)
    c[ImGuiCol_TitleBg] = { 0.07f, 0.08f, 0.10f, 1.00f };
    c[ImGuiCol_TitleBgActive] = { 0.07f, 0.08f, 0.10f, 1.00f };

    // Accent: electric indigo #5E6AD2
    const ImVec4 accent = { 0.37f, 0.42f, 0.82f, 1.00f };
    const ImVec4 accentHov = { 0.45f, 0.50f, 0.90f, 1.00f };
    const ImVec4 accentAct = { 0.30f, 0.35f, 0.72f, 1.00f };

    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = accentAct;
    c[ImGuiCol_Button] = { 0.17f, 0.19f, 0.24f, 1.00f };
    c[ImGuiCol_ButtonHovered] = accentHov;
    c[ImGuiCol_ButtonActive] = accentAct;
    c[ImGuiCol_Header] = { 0.37f, 0.42f, 0.82f, 0.20f };
    c[ImGuiCol_HeaderHovered] = { 0.37f, 0.42f, 0.82f, 0.35f };
    c[ImGuiCol_HeaderActive] = accent;
    c[ImGuiCol_Separator] = { 1.00f, 1.00f, 1.00f, 0.06f };
    c[ImGuiCol_SeparatorHovered] = accentHov;
    c[ImGuiCol_SeparatorActive] = accent;
    c[ImGuiCol_ResizeGrip] = { 0.00f, 0.00f, 0.00f, 0.00f };
    c[ImGuiCol_Tab] = { 0.13f, 0.15f, 0.19f, 1.00f };
    c[ImGuiCol_TabHovered] = accentHov;
    c[ImGuiCol_TabActive] = accent;
    c[ImGuiCol_ScrollbarBg] = { 0.09f, 0.10f, 0.12f, 1.00f };
    c[ImGuiCol_ScrollbarGrab] = { 0.22f, 0.25f, 0.31f, 1.00f };
    c[ImGuiCol_ScrollbarGrabHovered] = { 0.28f, 0.32f, 0.40f, 1.00f };
    c[ImGuiCol_ScrollbarGrabActive] = accent;
    c[ImGuiCol_Text] = { 0.92f, 0.93f, 0.95f, 1.00f };
    c[ImGuiCol_TextDisabled] = { 0.40f, 0.43f, 0.50f, 1.00f };
    c[ImGuiCol_PlotLines] = accent;
    c[ImGuiCol_PlotHistogram] = accent;
}

// ============================================================
//  Section header helper
// ============================================================

static void SectionLabel(const char* iconAndLabel, ImVec4 col)
{
    ImGui::TextColored(col, "%s", iconAndLabel);
    ImGui::Spacing();
}

// ============================================================
//  Main window
// ============================================================

static void DrawWindow()
{
    // Centre on first open
    static bool s_first = true;
    if (s_first)
    {
        ImVec2 disp = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos({ disp.x * 0.5f, disp.y * 0.5f },
            ImGuiCond_Always, { 0.5f, 0.5f });
        s_first = false;
    }
    ImGui::SetNextWindowSize({ 480.f, 560.f }, ImGuiCond_Once);

    const ImGuiWindowFlags wf =
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings;

    if (!ImGui::Begin(ICON_FA_FILM "  GrandFraps", &g_windowOpen, wf))
    {
        ImGui::End();
        return;
    }

    // ---- Status badge ----
    {
        const float bw = 120.f;
        ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - bw + 20.f);

        if (g_recording)
        {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, { 0.80f, 0.15f, 0.15f, 0.20f });
            ImGui::BeginChild("##badge", { bw, 26.f }, false,
                ImGuiWindowFlags_NoScrollbar);
            ImGui::SetCursorPos({ 10.f, 5.f });
            ImGui::TextColored({ 1.f, 0.35f, 0.35f, 1.f }, ICON_FA_CIRCLE "  REC");
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, { 0.20f, 0.75f, 0.45f, 0.12f });
            ImGui::BeginChild("##badge", { bw, 26.f }, false,
                ImGuiWindowFlags_NoScrollbar);
            ImGui::SetCursorPos({ 10.f, 5.f });
            ImGui::TextColored({ 0.30f, 0.85f, 0.55f, 1.f }, ICON_FA_CIRCLE "  Standby");
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }
        ImGui::SameLine(0, 0);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 26.f);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ---- Video section ----
    SectionLabel(ICON_FA_VIDEO "  Video", { 0.45f, 0.70f, 1.00f, 1.f });

    ImGui::PushStyleColor(ImGuiCol_ChildBg, { 0.11f, 0.13f, 0.16f, 1.f });
    ImGui::BeginChild("##video_block", { 0.f, 148.f }, false);

    ImGui::Spacing();

    // Width / Height side by side
    float w2 = (ImGui::GetContentRegionAvail().x - 10.f) * 0.5f;
    ImGui::SetNextItemWidth(w2);
    ImGui::InputInt("##w", &g_config.width);
    ImGui::SameLine(0, 10);
    ImGui::SetNextItemWidth(w2);
    ImGui::InputInt("##h", &g_config.height);
    ImGui::SameLine(0, 0);

    // Small labels
    {
        float prev = ImGui::GetCursorPosX();
        ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x * 0.0f + 4.f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::SetCursorPosX(40.f + w2 * 0.5f - 14.f);
        ImGui::Text("Width");
        ImGui::SameLine(40.f + w2 + 10.f + w2 * 0.5f - 14.f);
        ImGui::Text("Height");
        ImGui::PopStyleColor();
        (void)prev;
    }

    ImGui::Spacing();
    ImGui::SliderInt("FPS", &g_config.fps, 10, 240);
    ImGui::SliderInt("CRF (quality)", &g_config.crf, 0, 51);

    // Preset combo
    if (ImGui::Combo("Preset", &g_presetIdx, k_presets, IM_ARRAYSIZE(k_presets)))
        g_config.preset = k_presets[g_presetIdx];

    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::Spacing();

    // ---- Audio section ----
    SectionLabel(ICON_FA_VOLUME_UP "  Audio", { 0.35f, 0.88f, 0.60f, 1.f });

    ImGui::PushStyleColor(ImGuiCol_ChildBg, { 0.11f, 0.13f, 0.16f, 1.f });
    ImGui::BeginChild("##audio_block", { 0.f, 96.f }, false);

    ImGui::Spacing();
    ImGui::Checkbox("Enable audio", &g_config.enableAudio);
    ImGui::BeginDisabled(!g_config.enableAudio);
    ImGui::InputInt("Sample rate (Hz)", &g_config.sampleRate);
    ImGui::SliderInt("Channels", &g_config.channels, 1, 8);
    ImGui::EndDisabled();

    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::Spacing();

    // ---- Output section ----
    SectionLabel(ICON_FA_FOLDER_OPEN "  Output", { 1.00f, 0.80f, 0.30f, 1.f });

    ImGui::PushStyleColor(ImGuiCol_ChildBg, { 0.11f, 0.13f, 0.16f, 1.f });
    ImGui::BeginChild("##output_block", { 0.f, 68.f }, false);

    ImGui::Spacing();

    if (!g_pathInit)
    {
        strncpy_s(g_pathBuf, g_config.outputPath.c_str(), sizeof(g_pathBuf) - 1);
        g_pathInit = true;
    }
    if (ImGui::InputText("Output path", g_pathBuf, sizeof(g_pathBuf)))
        g_config.outputPath = g_pathBuf;

    ImGui::Checkbox("Enable logging", &g_config.enableLog);

    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ---- Record / Stop buttons ----
    const float btnW = (ImGui::GetContentRegionAvail().x - 10.f) * 0.5f;

    ImGui::BeginDisabled(g_recording);
    ImGui::PushStyleColor(ImGuiCol_Button, { 0.20f, 0.55f, 0.30f, 1.f });
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.25f, 0.70f, 0.40f, 1.f });
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, { 0.15f, 0.45f, 0.25f, 1.f });

    if (ImGui::Button(ICON_FA_PLAY "  Start", { btnW, 34.f }))
    {
        if (SUCCEEDED(g_writer.init(nullptr /* device passed from hook */, g_config)))
        {
            g_writer.startRecording();
            g_recording = true;
            SAMP::pSAMP->addMessageToChat(-1, "Recording started!");
            ImGui::InsertNotification({ ImGuiToastType_Success, 2500,
                                        "Recording started" });
        }
        else
        {
            ImGui::InsertNotification({ ImGuiToastType_Error, 3000,
                                        "Failed to initialize recorder" });
        }
    }
    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();

    ImGui::SameLine(0, 10);

    ImGui::BeginDisabled(!g_recording);
    ImGui::PushStyleColor(ImGuiCol_Button, { 0.60f, 0.15f, 0.15f, 1.f });
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.80f, 0.20f, 0.20f, 1.f });
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, { 0.50f, 0.10f, 0.10f, 1.f });

    if (ImGui::Button(ICON_FA_STOP "  Stop", { btnW, 34.f }))
    {
        g_recording = false;
        g_writer.stopRecording();
        SAMP::pSAMP->addMessageToChat(-1, "Recording stopped!");
        ImGui::InsertNotification({ ImGuiToastType_Info, 3000,
                                    "Recording saved" });
    }
    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();

    ImGui::End();
}

// ============================================================
//  Callbacks
// ============================================================

LRESULT __stdcall WndProcCallBack(
    SAMP::CallBacks::HookedStructs::stWndProcParams* params)
{
    if (g_initialized &&
        ImGui_ImplWin32_WndProcHandler(
            params->hWnd, params->uMsg, params->wParam, params->lParam))
        return 1;
    return 0;
}

HRESULT __stdcall D3DPresentHook(
    SAMP::CallBacks::HookedStructs::stPresentParams* params)
{
    if (!g_initialized)
        return D3D_OK;

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    if (g_windowOpen)
        DrawWindow();

    // Notifications (always rendered)
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg,
        { 43.f / 255.f, 43.f / 255.f, 43.f / 255.f, 180.f / 255.f });
    ImGui::RenderNotifications();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::EndFrame();
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());

    // Cursor management
    if (g_windowOpen)
    {
        if (!g_wasOpen)
        {
            SAMP::classes::pGame->SetCursorMode(
                SAMP::classes::CursorMode::CMODE_LOCKCAMANDCONTROL, true);
            ImGui::GetIO().MouseDrawCursor = true;
            g_wasOpen = true;
        }
    }
    else if (g_wasOpen)
    {
        g_wasOpen = false;
        SAMP::classes::pGame->SetCursorMode(
            SAMP::classes::CursorMode::CMODE_NONE, false);
        ImGui::GetIO().MouseDrawCursor = false;
    }

    if (g_recording)
        g_writer.captureFrame(params->pDevice);

    return D3D_OK;
}

HRESULT __stdcall D3DResetHook(
    SAMP::CallBacks::HookedStructs::stResetParams* /*params*/)
{
    if (g_initialized)
        ImGui_ImplDX9_InvalidateDeviceObjects();
    return D3D_OK;
}

void __cdecl CmdFraps(char* /*params*/)
{
    g_windowOpen = !g_windowOpen;
}

// ============================================================
//  Game loop — init once
// ============================================================

void __cdecl GameLoop()
{
    if (g_initialized)
        return;

    if (!SAMP::pSAMP->LoadAPI())
        return;

    // ImGui
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    // Fonts
    ImFontConfig icons_cfg;
    icons_cfg.MergeMode = true;
    icons_cfg.PixelSnapH = true;
    static const ImWchar icon_ranges[] = { 0xf000, 0xf999, 0 };

    io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\Arial.ttf", 16.f, nullptr,
        io.Fonts->GetGlyphRangesCyrillic());

    io.Fonts->AddFontFromFileTTF(
        "C:\\Games\\borgegta\\moonloader\\resource\\fa-solid-900.ttf",
        16.f, &icons_cfg, icon_ranges);

    ApplyTheme();

    ImGui_ImplWin32_Init(GetActiveWindow());
    ImGui_ImplDX9_Init(
        SAMP::CallBacks::pCallBackRegister->GetIDirect3DDevice9());

    // Default config
    g_config.fps = 60;
    g_config.crf = 15;
    g_config.outputPath = "GrandFraps/video.mp4";
    g_config.enableAudio = false;
    g_config.enableLog = true;
    g_config.logLevel = LogLevel::DEBUG;
    g_config.width = 1920;
    g_config.height = 1080;
    g_config.preset = k_presets[g_presetIdx];
    g_config.sampleRate = 44100;
    g_config.channels = 2;

    SAMP::pSAMP->addClientCommand("fraps", CmdFraps);
    SAMP::pSAMP->addMessageToChat(-1, "GrandFraps loaded — type /fraps to open");

    g_initialized = true;
}

// ============================================================
//  DllMain — minimal, no game logic here
// ============================================================

int __stdcall DllMain(HMODULE /*hModule*/, DWORD dwReason, LPVOID /*lpReserved*/)
{
    if (dwReason == DLL_PROCESS_ATTACH)
    {
        SAMP::Init();
        SAMP::CallBacks::pCallBackRegister->RegisterGameLoopCallback(GameLoop);
        SAMP::CallBacks::pCallBackRegister->RegisterWndProcCallback(WndProcCallBack);
        SAMP::CallBacks::pCallBackRegister->RegisterD3DCallback(D3DPresentHook);
        SAMP::CallBacks::pCallBackRegister->RegisterD3DCallback(D3DResetHook);
    }
    else if (dwReason == DLL_PROCESS_DETACH)
    {
        if (g_recording)
        {
            g_writer.stopRecording();
            g_recording = false;
        }
        if (g_initialized)
        {
            SAMP::pSAMP->unregisterChatCommand(CmdFraps);
            ImGui_ImplDX9_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
        }
        SAMP::ShutDown();
    }
    return TRUE;
}