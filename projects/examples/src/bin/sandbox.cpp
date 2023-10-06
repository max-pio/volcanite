#include <vvv/core/Renderer.hpp>
#include <vvv/passes/PassCompute.hpp>
#include <vvv/util/Paths.hpp>

#include <vvvwindow/entrypoint.hpp>
#include <vvvwindow/App.hpp>

#include <imgui/imgui.h>

#include <chrono>

using namespace vvv;

class SandboxRenderer : public Renderer, public WithGpuContext {
public:
    SandboxRenderer() : WithGpuContext(nullptr) {}

    void initGui(vvv::GuiInterface *gui) override {
        auto window = gui->get("sandbox");

        window->addBool(&m_guiShowHeader, "show available Uniforms");
        window->addCustomCode([this](){
              if (m_guiShowHeader)
                  ImGui::InputTextMultiline("##header", const_cast<char*>(m_sandboxHeaderText.c_str()), m_sandboxHeaderText.size() + 1, ImVec2(-1, 0), ImGuiInputTextFlags_ReadOnly);
          }, "");

        window->addLabel("hit F5 to compile shader.");

        window->addCustomCode([this]() {
            if (!m_compileErrorText.empty())
                //ImGui::InputTextMultiline("##error", const_cast<char*>(m_compileErrorText.c_str()), m_compileErrorText.size() + 1, ImVec2(-1, 0), ImGuiInputTextFlags_ReadOnly);
                ImGui::TextColored(ImVec4(255,0,0,255), "%s", m_compileErrorText.c_str());
        }, "");

        window->addCustomCode([this](){
            auto mousePos = ImGui::GetMousePos();
            m_mouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            m_mouseHeldDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
            if (m_mouseHeldDown) {
                m_mouseX = static_cast<int>(mousePos.x);
                m_mouseY = static_cast<int>(mousePos.y);
            }

            auto countLines = [](const char* str) -> int {
                int sum = 0;
                for (int i = 0; str[i] != '\0'; i++)
                    if (str[i] == '\n') sum++;
                return sum;
            };

            int headerLineNums = countLines(m_sandboxHeaderText.c_str()) + 1;
            int textLineNums = countLines(m_sandboxShaderText) + 1;
            m_sandboxShaderLineNums[0] = '\0';
            for (int i = headerLineNums; i < headerLineNums + textLineNums; i++) {
                auto num = std::to_string(i);
                strcat(m_sandboxShaderLineNums, num.c_str());
                if (i < headerLineNums + textLineNums - 1)
                    strcat(m_sandboxShaderLineNums, "\n");
            }

            auto size = ImGui::GetContentRegionAvail();
            auto textSize = ImGui::CalcTextSize(m_sandboxShaderLineNums, NULL, true);
            float codeHeight = textSize.y + ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
            float linumWidth = textSize.x + ImGui::GetStyle().FramePadding.x * 2.0f;

            ImGui::BeginChild("Code", ImVec2(size.x, size.y), false, ImGuiWindowFlags_HorizontalScrollbar);

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 0, 1)); // numbers in yellow
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGuiCol_ChildBg); // hide background. Black on default
            ImGui::InputTextMultiline("##linun", m_sandboxShaderLineNums, sizeof(m_sandboxShaderLineNums), ImVec2(linumWidth, codeHeight), ImGuiInputTextFlags_ReadOnly);
            ImGui::PopStyleColor(2);

            ImGui::SameLine(linumWidth);

            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f,0.2f,0.2f,1));
            ImGui::InputTextMultiline("##code", m_sandboxShaderText, sizeof(m_sandboxShaderText), ImVec2(-1, codeHeight), ImGuiInputTextFlags_AllowTabInput);
            ImGui::PopStyleColor(1);

            ImGui::EndChild();
        }, "");
    }

    void initResources(vvv::GpuContextRwPtr ctx) override {
        setCtx(ctx);
        m_shaderSourceFilepath = Paths::getTempFileWithName("sandbox.comp");

        // load header and default glsl code
        std::stringstream header_ss, default_ss;
        header_ss << std::ifstream(Paths::findShaderPath("sandbox_header.glsl")).rdbuf();
        default_ss << std::ifstream(Paths::findShaderPath("sandbox_default.glsl")).rdbuf();
        m_sandboxHeaderText = header_ss.str();
        strcpy(m_sandboxShaderText, default_ss.str().c_str());
    }

    void initShaderResources() override {
        std::ofstream sourceFile(m_shaderSourceFilepath);
        // copy default header to shader containing unform and main definitions
        sourceFile << m_sandboxHeaderText;
        // copy shader text from ImGui-Editable buffer
        sourceFile << m_sandboxShaderText;
        sourceFile.close();

        GlslShaderRequest request{.shader_file_path = m_shaderSourceFilepath, .include_paths = Paths::getShaderDirectories(),
                                  .entry_point = "main", .stage = vk::ShaderStageFlagBits::eCompute, .label = "sandbox"};

        m_compileErrorText = "";
        ShaderCompileErrorCallback compileErrorCallback = [this](const ShaderCompileError& err) {
            Logger(ERROR) << err.errorText;
            m_compileErrorText = err.errorText;

            return ShaderCompileErrorCallbackAction::USE_PREVIOUS_CODE;
        };

        m_computePass = std::make_unique<SinglePassCompute>(SinglePassComputeSettings{.ctx = getCtx(), .label = "sandbox", .multiBuffering = getCtx()->getWsi()->stateInFlight()}, request, compileErrorCallback);
        m_computePass->allocateResources();
        m_optionsUniform = m_computePass->getUniformSet("options");
    }

    void initSwapchainResources() override {
        assert(m_computePass);

        const auto screen = getCtx()->getWsi()->getScreenExtent();
        uint32_t width = screen.width, height = screen.height;

        m_computePass->setGlobalInvocationSize(width, height);

        auto reflectOpts = TextureReflectionOptions{
            .width = width, .height = height, .format = vk::Format::eR8G8B8A8Unorm, .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled};
        m_outputTextures = m_computePass->reflectTextures("IMAGE_out", reflectOpts);

        for (auto& texture : *m_outputTextures)
            texture->initResources();

        m_lastFrameTime = m_startTime = std::chrono::system_clock::now();
        m_frameIdx = 0;
    }

    RendererOutput renderNextFrame(AwaitableList awaitBeforeExecution, BinaryAwaitableList awaitBinaryAwaitableList, vk::Semaphore *signalBinarySemaphore) override {
        assert(m_computePass);
        assert(m_optionsUniform);
        assert(m_outputTextures);

        Texture* outputTexture = m_outputTextures->getActive().get();
        m_computePass->setStorageImage("IMAGE_out", *outputTexture, vk::ImageLayout::eGeneral);

        auto layoutAwait = outputTexture->setImageLayout(vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits::eComputeShader, {.await = {awaitBeforeExecution}});

        auto currentTime = std::chrono::system_clock::now();
        m_optionsUniform->setUniform("iResolution", glm::vec3{outputTexture->width, outputTexture->height, 1});
        m_optionsUniform->setUniform("iTime", std::chrono::duration_cast<std::chrono::duration<float>>(currentTime- m_startTime).count());
        m_optionsUniform->setUniform("iTimeDelta", std::chrono::duration_cast<std::chrono::duration<float>>(currentTime - m_lastFrameTime).count());
        m_optionsUniform->setUniform("iFrame", m_frameIdx);
        m_optionsUniform->setUniform("iMouse", glm::vec4{m_mouseX, outputTexture->height - m_mouseY - 1, m_mouseHeldDown, m_mouseClicked});
        m_optionsUniform->upload(m_outputTextures->getActiveIndex());
        m_lastFrameTime = currentTime;
        m_frameIdx++;

        auto await = m_computePass->execute({layoutAwait}, awaitBinaryAwaitableList);
        return {.texture = outputTexture, .renderingComplete = {await}};
    }

    void releaseSwapchain() override {
        m_outputTextures = nullptr;
    }

    void releaseShaderResources() override {
        m_optionsUniform = nullptr;
        m_computePass->freeResources();
        m_computePass = nullptr;
    }

    void releaseResources() override {

    }

    void releaseGui() override {

    }

private:
    std::unique_ptr<SinglePassCompute> m_computePass;
    std::shared_ptr<MultiBufferedTexture> m_outputTextures;
    std::shared_ptr<UniformReflected> m_optionsUniform;

    std::filesystem::path m_shaderSourceFilepath;

    std::chrono::time_point<std::chrono::system_clock> m_startTime;
    std::chrono::time_point<std::chrono::system_clock> m_lastFrameTime;
    int m_frameIdx = 0;
    int m_mouseX = 0;
    int m_mouseY = 0;
    bool m_mouseClicked = false;
    bool m_mouseHeldDown = false;

    bool m_guiShowHeader = false;
    std::string m_sandboxHeaderText;
    char m_sandboxShaderText[1024*1024] = "";
    char m_sandboxShaderLineNums[1024*1024] = "";

    std::string m_compileErrorText;
};

int sandbox_main(int argc, char *argv[]) {
    auto renderer = std::make_shared<SandboxRenderer>();
    auto app = Application::create("VVV Sandbox", renderer);
    app->setVSync(true);
    return app->exec();
}

ENTRYPOINT(sandbox_main)