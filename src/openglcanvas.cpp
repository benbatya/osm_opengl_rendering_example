#include "openglcanvas.h"

#include <shaders.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

wxDEFINE_EVENT(wxEVT_OPENGL_INITIALIZED, wxCommandEvent);

static const char *map_compute_source = R"(
#version 430 core
layout(local_size_x = 128) in;
layout(rgba8, binding = 0) uniform image2D imgOutput;

struct Vertex {
    float x, y;
    float r, g, b;
};

layout(std430, binding = 1) buffer VertexBuffer {
    Vertex vertices[];
};

layout(std430, binding = 2) buffer IndexBuffer {
    uint indices[];
};

uniform vec4 uBounds; // minLon, minLat, lonRange, latRange
uniform ivec2 uScreenSize;
uniform uint uNumIndices;

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= uNumIndices - 1) return;

    uint i1 = indices[idx];
    uint i2 = indices[idx+1];
    if (i1 == i2) return;

    Vertex v1 = vertices[i1];
    Vertex v2 = vertices[i2];
    
    vec2 p1 = vec2((v1.x - uBounds.x) / uBounds.z * uScreenSize.x,
                   (v1.y - uBounds.y) / uBounds.w * uScreenSize.y);
    vec2 p2 = vec2((v2.x - uBounds.x) / uBounds.z * uScreenSize.x,
                   (v2.y - uBounds.y) / uBounds.w * uScreenSize.y);

    vec2 dir = p2 - p1;
    float len = length(dir);
    if (len < 0.1) return;
    
    vec3 color = vec3(v1.r, v1.g, v1.b);
    for (float i = 0; i <= len; i += 0.5) {
        imageStore(imgOutput, ivec2(p1 + (dir/len) * i), vec4(color, 1.0));
    }
}
)";

static const char *display_v_source = R"(
#version 430 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
out vec2 TexCoord;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)";

static const char *display_f_source = R"(
#version 430 core
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D screenTexture;
void main() {
    FragColor = texture(screenTexture, TexCoord);
}
)";

// GL debug callback function used when KHR_debug is available. Logs
// messages (skips notifications) through wxLogError and stderr for
// high-severity messages.
static void GLDebugCallbackFunc(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
                                const GLchar *message, const void *userParam) {
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) {
        return;
    }

    std::ostringstream ss;
    ss << "GL Debug (id=" << id << ") ";

    switch (source) {
    case GL_DEBUG_SOURCE_API:
        ss << "source=API ";
        break;
    case GL_DEBUG_SOURCE_WINDOW_SYSTEM:
        ss << "source=WindowSystem ";
        break;
    case GL_DEBUG_SOURCE_SHADER_COMPILER:
        ss << "source=ShaderCompiler ";
        break;
    case GL_DEBUG_SOURCE_THIRD_PARTY:
        ss << "source=ThirdParty ";
        break;
    case GL_DEBUG_SOURCE_APPLICATION:
        ss << "source=Application ";
        break;
    case GL_DEBUG_SOURCE_OTHER:
    default:
        ss << "source=Other ";
        break;
    }

    switch (type) {
    case GL_DEBUG_TYPE_ERROR:
        ss << "type=Error ";
        break;
    case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
        ss << "type=DeprecatedBehavior ";
        break;
    case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
        ss << "type=UndefinedBehavior ";
        break;
    case GL_DEBUG_TYPE_PORTABILITY:
        ss << "type=Portability ";
        break;
    case GL_DEBUG_TYPE_PERFORMANCE:
        ss << "type=Performance ";
        break;
    case GL_DEBUG_TYPE_OTHER:
    default:
        ss << "type=Other ";
        break;
    }

    switch (severity) {
    case GL_DEBUG_SEVERITY_HIGH:
        ss << "severity=HIGH ";
        break;
    case GL_DEBUG_SEVERITY_MEDIUM:
        ss << "severity=MEDIUM ";
        break;
    case GL_DEBUG_SEVERITY_LOW:
        ss << "severity=LOW ";
        break;
    case GL_DEBUG_SEVERITY_NOTIFICATION:
    default:
        ss << "severity=NOTIFICATION ";
        break;
    }

    ss << "message=" << message;

    std::cerr << ss.str() << std::endl;
}

OpenGLCanvas::OpenGLCanvas(wxWindow *parent, const wxGLAttributes &canvasAttrs) : wxGLCanvas(parent, canvasAttrs) {
    wxGLContextAttrs ctxAttrs;
    ctxAttrs.PlatformDefaults().CoreProfile().OGLVersion(4, 3).EndList();
    openGLContext_ = new wxGLContext(this, nullptr, &ctxAttrs);

    if (!openGLContext_->IsOK()) {
        wxMessageBox("This sample needs an OpenGL 3.3 capable driver.", "OpenGL version error",
                     wxOK | wxICON_INFORMATION, this);
        delete openGLContext_;
        openGLContext_ = nullptr;
    }

    Bind(wxEVT_PAINT, &OpenGLCanvas::OnPaint, this);
    Bind(wxEVT_SIZE, &OpenGLCanvas::OnSize, this);
    Bind(wxEVT_LEFT_DOWN, &OpenGLCanvas::OnLeftDown, this);
    Bind(wxEVT_LEFT_UP, &OpenGLCanvas::OnLeftUp, this);
    Bind(wxEVT_MOTION, &OpenGLCanvas::OnMouseMotion, this);

    // Bind mouse wheel events for zooming
    Bind(wxEVT_MOUSEWHEEL, &OpenGLCanvas::OnMouseWheel, this);

    // Bind zoom gesture events
    Bind(wxEVT_GESTURE_ZOOM, &OpenGLCanvas::OnZoomGesture, this);
    EnableTouchEvents(wxTOUCH_ZOOM_GESTURE);

    timer_.SetOwner(this);
    this->Bind(wxEVT_TIMER, &OpenGLCanvas::OnTimer, this);

    // TODO: try to make the FPS higher
    constexpr auto FPS = 60.0;
    timer_.Start(1000 / FPS);
}

void OpenGLCanvas::SetData(const OSMLoader::OSMData &data, const osmium::Box &bounds) {
    const auto &ways = data.first;
    // TODO: render relationships
    const auto &areas = data.second;
    coordinateBounds_ = bounds;
    // Find the longest ways and store only those for testing
    storedRoutes_.clear();
    storedAreas_.clear();

    // const size_t NUM_WAYS = std::min(ways.size(), static_cast<size_t>(1));

    // std::unordered_map<size_t, std::vector<osmium::object_id_type>> lenIdMap;

    // for (const auto &way : ways) {
    //     auto length = way.second.nodes.size();
    //     lenIdMap[length].push_back(way.first);
    // }

    // std::vector<size_t> sortedLengths;
    // for (const auto &pair : lenIdMap) {
    //     sortedLengths.push_back(pair.first);
    // }
    // std::sort(sortedLengths.rbegin(), sortedLengths.rend());

    // size_t count = 0;
    // for (size_t length : sortedLengths) {
    //     for (auto wayId : lenIdMap[length]) {
    //         if (count >= NUM_WAYS)
    //             break;
    //         storedRoutes_[wayId] = ways.at(wayId);
    //         std::cout << "Selected way ID " << wayId << ", '"
    //                   << storedRoutes_.at(wayId).name << "' with length "
    //                   << length << std::endl;
    //         ++count;
    //     }
    // }

    // take first N ways
    // size_t count = 0;
    // for (const auto &route : ways) {
    //     if (count >= NUM_WAYS) {
    //         break;
    //     }
    //     ++count;
    //     storedRoutes_[route.first] = route.second;
    // }

    // Take all ways
    storedRoutes_ = ways;
    storedAreas_ = areas;

    // add the boundary
    OSMLoader::Route_t boundsWay{};
    boundsWay.id = 42;
    boundsWay.tags[NAME_TAG] = "bounds";
    boundsWay.nodes = {osmium::Location(bounds.left(), bounds.bottom()),
                       osmium::Location(bounds.right(), bounds.bottom()),
                       osmium::Location(bounds.right(), bounds.top()), osmium::Location(bounds.left(), bounds.top()),
                       osmium::Location(bounds.left(), bounds.bottom())};
    boundsWay.tags[HIGHWAY_TAG] = "footpath";
    storedRoutes_[boundsWay.id] = boundsWay;

    UpdateBuffersFromRoutes();
}

void OpenGLCanvas::AddLineStripAdjacencyToBuffers(const OSMLoader::Coordinates &coords, const Color_t &color,
                                                  std::vector<float> &vertices, std::vector<GLuint> &indices,
                                                  size_t &indexOffset) {
    if (coords.size() < 2) {
        return;
    }

    // Store the starting index for this line strip in the vertices array
    GLuint base = static_cast<GLuint>(vertices.size() / 5);

    // Add vertices for the current line strip
    for (const auto &loc : coords) {
        assert(loc.valid());
        double lon = loc.lon();
        double lat = loc.lat();
        // Store raw lon/lat in vertex attributes; shader will normalize
        vertices.push_back(static_cast<float>(lon));
        vertices.push_back(static_cast<float>(lat));
        vertices.push_back(color[0]);
        vertices.push_back(color[1]);
        vertices.push_back(color[2]);
    }

    // Indices for GL_LINE_STRIP_ADJACENCY: duplicate first and last
    // This is required for the geometry shader to calculate normals for the end segments.
    GLuint countHere = 0;

    // Start: duplicate first vertex
    indices.push_back(base);
    countHere += 1;

    // Add all vertices of the current line strip
    for (size_t i = 0; i < (vertices.size() / 5) - base; ++i) {
        indices.push_back(base + static_cast<GLuint>(i));
        ++countHere;
    }

    // End: duplicate last vertex
    indices.push_back(base + static_cast<GLuint>((vertices.size() / 5) - 1 - base));
    countHere += 1;

    // Record draw command (count, byte offset)
    size_t startByteOffset = indexOffset * sizeof(GLuint);
    drawCommands_.emplace_back(static_cast<GLsizei>(countHere), startByteOffset);
    indexOffset += countHere;
}

void OpenGLCanvas::UpdateBuffersFromRoutes() {
    if (!isOpenGLInitialized_) {
        return;
    }

    // Build vertex and index arrays from storedRoutes_. Vertex layout:
    // x,y,r,g,b
    std::vector<float> vertices;
    std::vector<GLuint> indices;
    drawCommands_.clear();

    if (storedRoutes_.empty()) {
        elementCount_ = 0;
        return;
    }

    using MapType = std::unordered_map<std::string, Color_t>;
    static MapType HIGHWAY2COLOR = {{"motorway", {1.0f, 0.35f, 0.35f}},   {"motorway_link", {1.0f, 0.6f, 0.6f}},
                                    {"secondary", {1.0f, 0.75f, 0.4f}},   {"tertiary", {1.0f, 1.0f, 0.6f}},
                                    {"residential", {1.0f, 1.0f, 1.0f}},  {"unclassified", {0.95f, 0.95f, 0.95f}},
                                    {"service", {0.8f, 0.8f, 0.8f}},      {"track", {0.65f, 0.55f, 0.4f}},
                                    {"pedestrian", {0.85f, 0.8f, 0.85f}}, {"footway", {0.9f, 0.7f, 0.7f}},
                                    {"path", {0.6f, 0.7f, 0.6f}},         {"steps", {0.7f, 0.4f, 0.4f}},
                                    {"platform", {0.6f, 0.6f, 0.8f}}};
    Color_t DEFAULT_COLOR = {0.5f, 0.5f, 0.5f};
    Color_t AREA_COLOR = {0.2f, 0.89f, 0.1f};

    size_t indexOffset = 0;

    for (const auto &entry : storedRoutes_) {
        const auto &coords = entry.second;
        if (coords.nodes.size() < 2)
            continue;

        GLuint base = static_cast<GLuint>(vertices.size() / 5);
        const std::string &highwayType = entry.second.tags.count(HIGHWAY_TAG) ? entry.second.tags.at(HIGHWAY_TAG) : "";
        const auto &color = HIGHWAY2COLOR.count(highwayType) ? HIGHWAY2COLOR.at(highwayType) : DEFAULT_COLOR;
        AddLineStripAdjacencyToBuffers(coords.nodes, color, vertices, indices, indexOffset);
    }

    elementCount_ = static_cast<GLsizei>(indices.size());

    // Create VAO/VBO/EBO if necessary and upload
    if (VAO_ == 0)
        glGenVertexArrays(1, &VAO_);
    glBindVertexArray(VAO_);

    if (VBO_ == 0)
        glGenBuffers(1, &VBO_);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_);
    if (!vertices.empty())
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    if (EBO_ == 0)
        glGenBuffers(1, &EBO_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO_);
    if (!indices.empty())
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), indices.data(), GL_STATIC_DRAW);

    // vertex attributes
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void *>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void *>(2 * sizeof(float)));

    // Unbind
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void OpenGLCanvas::CompileShaderProgram() {
    auto compile = [](GLenum type, const char *src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint success;
        glGetShaderiv(s, GL_COMPILE_STATUS, &success);
        if (!success) {
            char infoLog[512];
            glGetShaderInfoLog(s, 512, nullptr, infoLog);
            std::cerr << "Shader compilation error: " << infoLog << std::endl;
        }
        return s;
    };

    GLuint cs = compile(GL_COMPUTE_SHADER, map_compute_source);
    map_compute_program_ = glCreateProgram();
    glAttachShader(map_compute_program_, cs);
    glLinkProgram(map_compute_program_);
    glDeleteShader(cs);

    GLuint vs = compile(GL_VERTEX_SHADER, display_v_source);
    GLuint fs = compile(GL_FRAGMENT_SHADER, display_f_source);
    display_program_ = glCreateProgram();
    glAttachShader(display_program_, vs);
    glAttachShader(display_program_, fs);
    glLinkProgram(display_program_);
    glDeleteShader(vs);
    glDeleteShader(fs);
}

OpenGLCanvas::~OpenGLCanvas() {
    glDeleteVertexArrays(1, &VAO_);
    glDeleteBuffers(1, &VBO_);
    glDeleteBuffers(1, &EBO_);
    glDeleteTextures(1, &render_texture_);
    glDeleteVertexArrays(1, &quad_vao_);
    glDeleteBuffers(1, &quad_vbo_);
    glDeleteProgram(map_compute_program_);
    glDeleteProgram(display_program_);

    delete openGLContext_;
}

bool OpenGLCanvas::InitializeOpenGLFunctions() {
    GLenum err = glewInit();

    if (GLEW_OK != err) {
        wxLogError("OpenGL GLEW initialization failed: %s", reinterpret_cast<const char *>(glewGetErrorString(err)));
        return false;
    }

    wxLogDebug("Status: Using GLEW %s", reinterpret_cast<const char *>(glewGetString(GLEW_VERSION)));

    return true;
}

bool OpenGLCanvas::InitializeOpenGL() {
    if (!openGLContext_) {
        return false;
    }

    SetCurrent(*openGLContext_);

    if (!InitializeOpenGLFunctions()) {
        wxMessageBox("Error: Could not initialize OpenGL function pointers.", "OpenGL initialization error",
                     wxOK | wxICON_INFORMATION, this);
        return false;
    }

    wxLogDebug("OpenGL version: %s", reinterpret_cast<const char *>(glGetString(GL_VERSION)));
    wxLogDebug("OpenGL vendor: %s", reinterpret_cast<const char *>(glGetString(GL_VENDOR)));

    // Setup GL debug callback if available (KHR_debug)
    if (GLEW_KHR_debug) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);

        glDebugMessageCallback(GLDebugCallbackFunc, this);

        // Enable all messages (you can filter with glDebugMessageControl)
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
        wxLogDebug("KHR_debug is available: GL debug output enabled");
    } else {
        wxLogDebug("KHR_debug not available; GL debug output disabled");
    }

    // Setup quad for display
    float quadVertices[] = {
        -1.0f, 1.0f, 0.0f, 1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 0.0f,
    };
    glGenVertexArrays(1, &quad_vao_);
    glGenBuffers(1, &quad_vbo_);
    glBindVertexArray(quad_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));

    CompileShaderProgram();

    isOpenGLInitialized_ = true;

    // If ways were provided before GL initialization, upload them now.
    UpdateBuffersFromRoutes();

    openGLInitializationTime_ = std::chrono::high_resolution_clock::now();
    // initialize FPS timer state
    lastFpsUpdateTime_ = std::chrono::high_resolution_clock::now();
    framesSinceLastFps_ = 0;

    auto initRect = GetClientRect();
    initRect.SetSize(GetSize() * GetContentScaleFactor());

    this->viewportBounds_ = initRect;
    // std::cout << "InitializeOpenGL: called\n";

    wxCommandEvent evt(wxEVT_OPENGL_INITIALIZED);
    evt.SetEventObject(this);
    ProcessWindowEvent(evt);

    return true;
}

void OpenGLCanvas::OnPaint(wxPaintEvent &WXUNUSED(event)) {
    wxPaintDC dc(this);

    if (!isOpenGLInitialized_) {
        return;
    }

    SetCurrent(*openGLContext_);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float clearColor = 0.87f;
    glClearColor(clearColor, clearColor, clearColor, 0.5f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    auto size = GetClientSize() * GetContentScaleFactor();
    wxPoint bottomLeft{};
    wxPoint topRight(size.x, size.y);

    auto bottomLeftCoord = mapViewport2OSM(bottomLeft);
    auto topRightCoord = mapViewport2OSM(topRight);
    double minLon = bottomLeftCoord.lon();
    double minLat = bottomLeftCoord.lat();

    double lonRange = (topRightCoord.lon() - bottomLeftCoord.lon());
    double latRange = (topRightCoord.lat() - bottomLeftCoord.lat());

    if (lonRange == 0.0)
        lonRange = 1.0;
    if (latRange == 0.0)
        latRange = 1.0;

    // 1. Clear texture
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, render_texture_, 0);
    glClearColor(clearColor, clearColor, clearColor, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);

    // 2. Dispatch compute
    glUseProgram(map_compute_program_);
    glUniform4f(glGetUniformLocation(map_compute_program_, "uBounds"), static_cast<float>(minLon),
                static_cast<float>(minLat), static_cast<float>(lonRange), static_cast<float>(latRange));
    glUniform2i(glGetUniformLocation(map_compute_program_, "uScreenSize"), size.x, size.y);
    glUniform1ui(glGetUniformLocation(map_compute_program_, "uNumIndices"), static_cast<GLuint>(elementCount_));

    glBindImageTexture(0, render_texture_, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, VBO_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, EBO_);

    glDispatchCompute((elementCount_ + 127) / 128, 1, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    // 3. Draw quad
    glUseProgram(display_program_);
    glBindVertexArray(quad_vao_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, render_texture_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);

    SwapBuffers();

    // Update FPS counters and draw overlay text
    ++framesSinceLastFps_;
    auto now = std::chrono::high_resolution_clock::now();
    auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFpsUpdateTime_);
    if (dur.count() >= 250) { // update FPS every 250ms for smoother display
        float seconds = dur.count() / 1000.0f;
        if (seconds > 0.0f) {
            fps_ = static_cast<float>(framesSinceLastFps_) / seconds;
        }
        framesSinceLastFps_ = 0;
        lastFpsUpdateTime_ = now;
    }

    // Draw FPS using wx overlay drawing so it's on top of GL content.
    // Use a small margin from the top-left corner.
    wxClientDC overlayDc(this);
    wxFont font = overlayDc.GetFont();
    font.SetWeight(wxFONTWEIGHT_BOLD);
    font.SetPointSize(10);
    overlayDc.SetFont(font);
    overlayDc.SetTextForeground(*wxBLACK);
    std::ostringstream ss;
    ss.setf(std::ios::fixed);
    ss.precision(1);
    ss << "FPS: " << fps_;
    const std::string fpsText = ss.str();
    const int margin = 8;
    overlayDc.DrawText(fpsText, margin, margin);
}

void OpenGLCanvas::OnSize(wxSizeEvent &event) {
    // std::cout << "OnSize: called" << std::endl;

    bool firstApperance = IsShownOnScreen() && !isOpenGLInitialized_;

    if (firstApperance) {
        InitializeOpenGL();
    }

    if (isOpenGLInitialized_) {
        SetCurrent(*openGLContext_);

        auto viewPortSize = event.GetSize() * GetContentScaleFactor();
        glViewport(0, 0, viewPortSize.x, viewPortSize.y);

        if (viewportSize_.GetWidth() > 0) {
            auto vpPos = viewportBounds_.GetPosition();
            vpPos += (viewPortSize - viewportSize_) / 2;
            viewportBounds_.SetPosition(vpPos);
        }

        // Save the viewportSize for later
        viewportSize_ = viewPortSize;

        // Recreate texture
        if (render_texture_)
            glDeleteTextures(1, &render_texture_);
        glGenTextures(1, &render_texture_);
        glBindTexture(GL_TEXTURE_2D, render_texture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, viewPortSize.x, viewPortSize.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }

    event.Skip();
}

void OpenGLCanvas::OnTimer(wxTimerEvent &WXUNUSED(event)) {
    if (isOpenGLInitialized_) {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - openGLInitializationTime_);
        elapsedSeconds_ = duration.count() / 1000.0f;
        Refresh(false);
    }
}

void OpenGLCanvas::OnLeftDown(wxMouseEvent &event) {
    isDragging_ = true;
    lastMousePos_ = event.GetPosition();
    lastMousePos_.y = GetClientSize().y - lastMousePos_.y; // flip Y
    // CaptureMouse();
}

void OpenGLCanvas::OnLeftUp(wxMouseEvent &event) {
    if (isDragging_) {
        isDragging_ = false;
        if (HasCapture())
            ReleaseMouse();
    }
}

void OpenGLCanvas::OnMouseMotion(wxMouseEvent &event) {
    if (!isDragging_)
        return;

    if (!event.Dragging() || !event.LeftIsDown())
        return;

    // std::cout << "OnMouseMotion: called" << std::endl;

    // compute pixel delta (use logical coordinates then account for content
    // scale)
    wxPoint pos = event.GetPosition();
    pos.y = GetClientSize().y - pos.y; // flip Y
    // use content scale factor to match viewport used for GL
    auto scale = GetContentScaleFactor();
    wxPoint posScaled = pos * scale;
    wxPoint lastScaled = lastMousePos_ * scale;

    // Update viewportBounds_
    auto newPos = viewportBounds_.GetPosition() + posScaled - lastScaled;
    this->viewportBounds_.SetPosition(newPos);

    lastMousePos_ = pos;

    // just request redraw; shader reads `coordinateBounds_` uniform during paint
    Refresh(false);
}

void OpenGLCanvas::OnMouseWheel(wxMouseEvent &event) {
    if (event.GetTimestamp() == 0 || event.GetTimestamp() == prevEventTimestamp_) {
        // Ignore synthetic events with duplicate timestamps (e.g. generated on
        // Windows when Alt key is pressed).
        return;
    }
    prevEventTimestamp_ = event.GetTimestamp();

    // Use wheel rotation to compute zoom steps
    const int rotation = event.GetWheelRotation();
    const int delta = event.GetWheelDelta();
    if (delta == 0 || rotation == 0)
        return;

    const int steps = (rotation / delta);

    // scale per step (<1 zooms in, >1 zooms out when steps negative)
    const double stepScale = 0.9;
    const double scale = std::pow(stepScale, steps);

    Zoom(scale, event.GetPosition());
}

void OpenGLCanvas::OnZoomGesture(wxZoomGestureEvent &event) {
    if (event.IsGestureStart()) {
        lastZoomFactor_ = 1.0;
    }

    double currentZoomFactor = event.GetZoomFactor();
    // Viewport range should scale inversely with magnification
    double scale = 1.0 / (currentZoomFactor / lastZoomFactor_);
    lastZoomFactor_ = currentZoomFactor;

    Zoom(scale, event.GetPosition());
}

void OpenGLCanvas::Zoom(double scale, const wxPoint &mousePosIn) {
    if (scale <= 0.0)
        return;

    double contentScale = GetContentScaleFactor();

    // Convert mouse position to the coordinate system used by viewportBounds_
    // (Physical pixels, Y-up to match dragging logic in OnMouseMotion)
    wxPoint mousePos = mousePosIn;
    mousePos.y = GetClientSize().y - mousePos.y;

    double mx = static_cast<double>(mousePos.x) * contentScale;
    double my = static_cast<double>(mousePos.y) * contentScale;

    // Current viewport box properties
    double oldX = static_cast<double>(viewportBounds_.x);
    double oldY = static_cast<double>(viewportBounds_.y);
    double oldW = static_cast<double>(viewportBounds_.width);
    double oldH = static_cast<double>(viewportBounds_.height);

    // Calculate relative position of mouse in the current viewport box [0, 1]
    double tx = (mx - oldX) / oldW;
    double ty = (my - oldY) / oldH;

    // New dimensions
    double newW = oldW * scale;
    double newH = oldH * scale;

    // New origin to keep the point under the mouse at the same relative position
    double newX = mx - tx * newW;
    double newY = my - ty * newH;

    // Update viewportBounds_
    viewportBounds_.x = static_cast<int>(std::round(newX));
    viewportBounds_.y = static_cast<int>(std::round(newY));
    viewportBounds_.width = static_cast<int>(std::round(newW));
    viewportBounds_.height = static_cast<int>(std::round(newH));

    // std::cout << "Zoom: called" << std::endl;

    Refresh(false);
}

osmium::Location OpenGLCanvas::mapViewport2OSM(const wxPoint &viewportCoord) {
    const auto extents = viewportBounds_.GetSize();

    const auto offset = viewportCoord - viewportBounds_.GetPosition();

    auto normalized = static_cast<double>(offset.x) / (extents.x - 1);
    double lon = coordinateBounds_.left() + normalized * (coordinateBounds_.right() - coordinateBounds_.left());

    normalized = static_cast<double>(offset.y) / (extents.y - 1);
    double lat = coordinateBounds_.bottom() + normalized * (coordinateBounds_.top() - coordinateBounds_.bottom());

    return osmium::Location(lon, lat);
}

wxPoint OpenGLCanvas::mapOSM2Viewport(const osmium::Location &coords) {
    const auto extents = viewportBounds_.GetSize();

    double lonRange = (coordinateBounds_.right() - coordinateBounds_.left());
    double latRange = (coordinateBounds_.top() - coordinateBounds_.bottom());

    double xNorm = (coords.lon() - coordinateBounds_.left()) / lonRange;
    double yNorm = (coords.lat() - coordinateBounds_.bottom()) / latRange;

    int x = static_cast<int>(xNorm * extents.x) + viewportBounds_.GetLeft();
    int y = static_cast<int>(yNorm * extents.y) + viewportBounds_.GetTop();

    return wxPoint(x, y);
}