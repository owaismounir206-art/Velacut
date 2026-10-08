// SPDX-License-Identifier: GPL-3.0-or-later
#include "GpuTransitions.h"

#include <QFile>
#include <QLoggingCategory>
#include <QMutex>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QThread>
#include <QVector2D>
#include <QVector4D>

#include <atomic>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>

Q_LOGGING_CATEGORY(lcGpuTransitions, "velacut.engine.gpu.transitions")

using namespace Qt::StringLiterals;

namespace velacut::engine {

namespace {

GpuTransitions *s_instance = nullptr;

constexpr int kMaxSide = 4096; // beyond this the CPU path is used (texture limits of old GPUs)

const char *kVertexShader = R"(
attribute vec2 position;
void main()
{
    gl_Position = vec4(position, 0.0, 1.0);
}
)";

// Straight RGBA8 (any stride) to premultiplied, packed rows: what the CPU kernels mix in.
void premultiply(fx::ConstImageView image, std::vector<std::uint8_t> &out)
{
    out.resize(static_cast<size_t>(image.width) * image.height * 4);
    std::uint8_t *d = out.data();
    for (int y = 0; y < image.height; ++y) {
        const std::uint8_t *s = image.row(y);
        for (int x = 0; x < image.width; ++x, s += 4, d += 4) {
            const int a = s[3];
            d[0] = static_cast<std::uint8_t>((s[0] * a + 127) / 255);
            d[1] = static_cast<std::uint8_t>((s[1] * a + 127) / 255);
            d[2] = static_cast<std::uint8_t>((s[2] * a + 127) / 255);
            d[3] = static_cast<std::uint8_t>(a);
        }
    }
}

double psnr(fx::ConstImageView a, fx::ConstImageView b)
{
    double sum = 0;
    for (int y = 0; y < a.height; ++y) {
        const std::uint8_t *p = a.row(y);
        const std::uint8_t *q = b.row(y);
        for (int i = 0; i < a.width * 4; ++i) {
            const double d = double(p[i]) - double(q[i]);
            sum += d * d;
        }
    }
    const double mse = sum / (double(a.width) * a.height * 4);
    return mse <= 0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
}

} // namespace

struct GpuTransitions::Private
{
    QOffscreenSurface *surface = nullptr; // created and destroyed on the GUI thread
    QThread thread;
    QObject *worker = nullptr; // lives in `thread`: jobs are invoked on it
    std::mutex jobs;           // one caller at a time

    // Only touched on the GPU thread.
    std::unique_ptr<QOpenGLContext> context;
    QOpenGLFunctions *gl = nullptr;
    std::map<int, std::unique_ptr<QOpenGLShaderProgram>> programs;
    QByteArray fragmentSource;
    GLuint textures[3] = {0, 0, 0}; // A, B, noise
    GLuint target = 0;
    GLuint framebuffer = 0;
    GLuint quad = 0;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> bufferA;
    std::vector<std::uint8_t> bufferB;
    std::vector<std::uint8_t> readback;
    bool checked = false;

    std::atomic<bool> failed{false};
    mutable QMutex textLock;
    QString failure;
    QString description;
    std::function<void(const QString &)> onFailure;

    void fail(const QString &why)
    {
        qCWarning(lcGpuTransitions).noquote() << "GPU transitions off, the CPU renders them:" << why;
        std::function<void(const QString &)> handler;
        {
            QMutexLocker lock(&textLock);
            failure = why;
            failed = true;
            handler = onFailure;
        }
        if (handler) {
            handler(why);
        }
    }
    bool ensureContext();
    bool ensureSize(int w, int h);
    QOpenGLShaderProgram *program(fx::TransitionKind kind);
    bool draw(fx::TransitionKind kind, fx::ImageView out, fx::ConstImageView a, fx::ConstImageView b, double progress,
              const fx::TransitionParams &params);
    bool selfCheck();
    void release();
};

bool GpuTransitions::Private::ensureContext()
{
    if (context) {
        return context->makeCurrent(surface);
    }
    context = std::make_unique<QOpenGLContext>();
    if (!context->create()) {
        fail(u"no OpenGL context"_s);
        return false;
    }
    if (!context->makeCurrent(surface)) {
        fail(u"the OpenGL context cannot be made current"_s);
        return false;
    }
    gl = context->functions();
    {
        QMutexLocker lock(&textLock);
        description = u"%1 · %2"_s.arg(QString::fromLatin1(reinterpret_cast<const char *>(gl->glGetString(GL_VERSION))),
                                        QString::fromLatin1(reinterpret_cast<const char *>(gl->glGetString(GL_RENDERER))));
    }
    QFile source(u":/velacut/gpu/transitions.frag"_s);
    if (!source.open(QIODevice::ReadOnly)) {
        fail(u"the transition shaders are missing"_s);
        return false;
    }
    fragmentSource = source.readAll();
    gl->glGenTextures(3, textures);
    for (int i = 0; i < 3; ++i) {
        gl->glBindTexture(GL_TEXTURE_2D, textures[i]);
        const GLint filter = i == 2 ? GL_NEAREST : GL_LINEAR;
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    gl->glGenTextures(1, &target);
    gl->glBindTexture(GL_TEXTURE_2D, target);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glGenFramebuffers(1, &framebuffer);
    const GLfloat corners[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    gl->glGenBuffers(1, &quad);
    gl->glBindBuffer(GL_ARRAY_BUFFER, quad);
    gl->glBufferData(GL_ARRAY_BUFFER, sizeof(corners), corners, GL_STATIC_DRAW);
    return gl->glGetError() == GL_NO_ERROR || (fail(u"OpenGL error while preparing"_s), false);
}

bool GpuTransitions::Private::ensureSize(int w, int h)
{
    if (w == width && h == height) {
        return true;
    }
    width = w;
    height = h;
    // The pseudo-random values of the CPU kernels, pixel by pixel, as 16 bits (red: high byte, green: low byte).
    std::vector<std::uint8_t> noise(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const auto q = static_cast<unsigned>(std::lround(fx::transitionNoise(x, y) * 65535.0f));
            std::uint8_t *d = &noise[(static_cast<size_t>(y) * w + x) * 4];
            d[0] = static_cast<std::uint8_t>(q >> 8);
            d[1] = static_cast<std::uint8_t>(q & 0xff);
            d[2] = 0;
            d[3] = 255;
        }
    }
    gl->glBindTexture(GL_TEXTURE_2D, textures[2]);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, noise.data());
    for (int i = 0; i < 2; ++i) {
        gl->glBindTexture(GL_TEXTURE_2D, textures[i]);
        gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
    gl->glBindTexture(GL_TEXTURE_2D, target);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
    if (gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fail(u"the framebuffer for %1×%2 is not complete"_s.arg(w).arg(h));
        return false;
    }
    return true;
}

QOpenGLShaderProgram *GpuTransitions::Private::program(fx::TransitionKind kind)
{
    const int key = static_cast<int>(kind);
    if (const auto it = programs.find(key); it != programs.end()) {
        return it->second.get();
    }
    const std::string_view name = fx::transitionKindName(kind);
    const QByteArray version = context->isOpenGLES() ? QByteArrayLiteral("#version 100\n") : QByteArrayLiteral("#version 110\n");
    const QByteArray define = "#define K_" + QByteArray(name.data(), static_cast<qsizetype>(name.size())) + '\n';
    auto program = std::make_unique<QOpenGLShaderProgram>();
    if (!program->addShaderFromSourceCode(QOpenGLShader::Vertex, version + kVertexShader)
        || !program->addShaderFromSourceCode(QOpenGLShader::Fragment, version + define + fragmentSource)
        || !(program->bindAttributeLocation("position", 0), program->link())) {
        fail(u"the shader of %1 does not compile: %2"_s.arg(QString::fromLatin1(name.data(), static_cast<qsizetype>(name.size())),
                                                              program->log().trimmed()));
        return nullptr;
    }
    QOpenGLShaderProgram *raw = program.get();
    programs.emplace(key, std::move(program));
    return raw;
}

bool GpuTransitions::Private::draw(fx::TransitionKind kind, fx::ImageView out, fx::ConstImageView a, fx::ConstImageView b,
                                   double progress, const fx::TransitionParams &params)
{
    const int w = out.width;
    const int h = out.height;
    QOpenGLShaderProgram *shader = program(kind);
    if (!shader || !ensureSize(w, h)) {
        return false;
    }
    premultiply(a, bufferA);
    premultiply(b, bufferB);
    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, textures[0]);
    gl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, bufferA.data());
    gl->glActiveTexture(GL_TEXTURE1);
    gl->glBindTexture(GL_TEXTURE_2D, textures[1]);
    gl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, bufferB.data());
    gl->glActiveTexture(GL_TEXTURE2);
    gl->glBindTexture(GL_TEXTURE_2D, textures[2]);

    gl->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    gl->glViewport(0, 0, w, h);
    gl->glDisable(GL_BLEND);
    shader->bind();
    shader->setUniformValue("texA", 0);
    shader->setUniformValue("texB", 1);
    shader->setUniformValue("noise", 2);
    shader->setUniformValue("size", QVector2D(float(w), float(h)));
    shader->setUniformValue("t", float(std::clamp(progress, 0.0, 1.0)));
    shader->setUniformValue("soft", float(std::max(0.0001, params.softness)));
    const auto &c = params.color;
    shader->setUniformValue("customColor", QVector4D(c[0] * c[3], c[1] * c[3], c[2] * c[3], c[3]));
    gl->glBindBuffer(GL_ARRAY_BUFFER, quad);
    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    readback.resize(static_cast<size_t>(w) * h * 4);
    gl->glPixelStorei(GL_PACK_ALIGNMENT, 4);
    gl->glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, readback.data());
    if (const GLenum error = gl->glGetError(); error != GL_NO_ERROR) {
        fail(u"OpenGL error 0x%1 while drawing"_s.arg(error, 0, 16));
        return false;
    }
    // OpenGL's rows go upwards; the image's go downwards.
    for (int y = 0; y < h; ++y) {
        std::memcpy(out.row(y), &readback[static_cast<size_t>(h - 1 - y) * w * 4], static_cast<size_t>(w) * 4);
    }
    return true;
}

// The first job: a few transitions drawn on both paths must agree, or the GPU path is not trusted (a driver that
// compiles but computes differently would otherwise change the export).
bool GpuTransitions::Private::selfCheck()
{
    const int w = 96;
    const int h = 54;
    std::vector<std::uint8_t> a(w * h * 4);
    std::vector<std::uint8_t> b(w * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::uint8_t *pa = &a[(y * w + x) * 4];
            std::uint8_t *pb = &b[(y * w + x) * 4];
            pa[0] = static_cast<std::uint8_t>(x * 255 / w);
            pa[1] = static_cast<std::uint8_t>(y * 255 / h);
            pa[2] = 90;
            pa[3] = 255;
            pb[0] = 30;
            pb[1] = static_cast<std::uint8_t>(255 - x * 255 / w);
            pb[2] = static_cast<std::uint8_t>(((x / 8 + y / 8) % 2) * 200 + 40);
            pb[3] = 255;
        }
    }
    std::vector<std::uint8_t> cpu(w * h * 4);
    std::vector<std::uint8_t> gpu(w * h * 4);
    const fx::ConstImageView av(a.data(), w, h, w * 4);
    const fx::ConstImageView bv(b.data(), w, h, w * 4);
    for (const fx::TransitionKind kind : {fx::TransitionKind::Dissolve, fx::TransitionKind::Iris, fx::TransitionKind::MosaicWipe,
                                          fx::TransitionKind::SpinCw}) {
        fx::renderTransition(kind, fx::ImageView{cpu.data(), w, h, w * 4}, av, bv, 0.4, {}, 0, h);
        if (!draw(kind, fx::ImageView{gpu.data(), w, h, w * 4}, av, bv, 0.4, {})) {
            return false;
        }
        const double quality = psnr(fx::ConstImageView(cpu.data(), w, h, w * 4), fx::ConstImageView(gpu.data(), w, h, w * 4));
        if (quality < 35.0) {
            const std::string_view name = fx::transitionKindName(kind);
            fail(u"the GPU result of %1 differs from the CPU (PSNR %2 dB)"_s
                     .arg(QString::fromLatin1(name.data(), static_cast<qsizetype>(name.size())))
                     .arg(quality, 0, 'f', 1));
            return false;
        }
    }
    qCInfo(lcGpuTransitions).noquote() << "GPU transitions on:" << description;
    return true;
}

void GpuTransitions::Private::release()
{
    if (!context) {
        return;
    }
    context->makeCurrent(surface);
    programs.clear();
    if (gl) {
        gl->glDeleteTextures(3, textures);
        gl->glDeleteTextures(1, &target);
        gl->glDeleteFramebuffers(1, &framebuffer);
        gl->glDeleteBuffers(1, &quad);
    }
    context->doneCurrent();
    context.reset();
}

GpuTransitions::GpuTransitions()
    : d(std::make_unique<Private>())
{
    d->surface = new QOffscreenSurface();
    d->surface->create();
    d->worker = new QObject();
    d->worker->moveToThread(&d->thread);
    d->thread.setObjectName(u"velacut-gpu-transitions"_s);
    d->thread.start();
}

GpuTransitions::~GpuTransitions()
{
    if (d->thread.isRunning()) {
        // The context goes on its own thread; the worker is deleted there too, when the thread ends.
        QMetaObject::invokeMethod(d->worker, [this] { d->release(); }, Qt::BlockingQueuedConnection);
        d->worker->deleteLater();
        d->thread.quit();
        d->thread.wait();
    } else {
        delete d->worker;
    }
    delete d->surface;
}

void GpuTransitions::initialize()
{
    if (!s_instance) {
        s_instance = new GpuTransitions();
    }
}

void GpuTransitions::shutdown()
{
    delete s_instance;
    s_instance = nullptr;
}

GpuTransitions *GpuTransitions::instance()
{
    return s_instance;
}

bool GpuTransitions::available() const
{
    return !d->failed && d->surface->isValid();
}

QString GpuTransitions::failure() const
{
    QMutexLocker lock(&d->textLock);
    return d->failure;
}

void GpuTransitions::setFailureHandler(std::function<void(const QString &)> handler)
{
    QMutexLocker lock(&d->textLock);
    d->onFailure = std::move(handler);
}

QString GpuTransitions::description() const
{
    QMutexLocker lock(&d->textLock);
    return d->description;
}

bool GpuTransitions::render(fx::TransitionKind kind, fx::ImageView out, fx::ConstImageView a, fx::ConstImageView b,
                            double progress, const fx::TransitionParams &params)
{
    // The ends are plain copies (cheaper on the CPU); sizes must match, and stay within old GPUs' limits.
    if (!available() || progress <= 0.0 || progress >= 1.0 || a.width != out.width || b.width != out.width
        || a.height != out.height || b.height != out.height || out.width > kMaxSide || out.height > kMaxSide || out.width < 2
        || out.height < 2) {
        return false;
    }
    std::lock_guard lock(d->jobs);
    bool done = false;
    const auto job = [&] {
        if (!d->ensureContext()) {
            return;
        }
        if (!d->checked) {
            d->checked = true;
            if (!d->selfCheck()) {
                return;
            }
        }
        done = !d->failed && d->draw(kind, out, a, b, progress, params);
    };
    if (QThread::currentThread() == &d->thread) {
        job();
    } else {
        QMetaObject::invokeMethod(d->worker, job, Qt::BlockingQueuedConnection);
    }
    return done;
}

} // namespace velacut::engine
