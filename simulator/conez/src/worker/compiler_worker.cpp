#include "compiler_worker.h"

#include <QFileInfo>
#include <QFile>
#include <csetjmp>
#include <cstdint>

/*
 * Forward-declare only the embedded compiler API we need.
 * We can't include both bas2wasm.h and c2wasm.h in the same TU because
 * they share type/enum names (Buf, ImportDef, IMP_*).
 */
extern "C" {

/* --- bas2wasm embedded API --- */
typedef void (*bw_diag_fn)(const char *msg, void *ctx);
extern bw_diag_fn bw_on_error;
extern bw_diag_fn bw_on_info;
extern void *bw_cb_ctx;
extern jmp_buf bw_bail;

typedef struct { uint8_t *data; int len, cap; } bw_Buf;
#define Buf bw_Buf
bw_Buf bas2wasm_compile_buffer(const char *src, int len);
void bas2wasm_reset(void);
void bw_buf_free(bw_Buf *b);
#undef Buf

/* --- c2wasm embedded API --- */
typedef void (*cw_diag_fn)(const char *msg, void *ctx);
extern cw_diag_fn cw_on_error;
extern cw_diag_fn cw_on_info;
extern void *cw_cb_ctx;
extern jmp_buf cw_bail;

typedef struct { uint8_t *data; int len, cap; } cw_Buf;
#define Buf cw_Buf
cw_Buf c2wasm_compile_buffer(const char *src, int len, const char *filename);
void c2wasm_reset(void);
void cw_buf_free(cw_Buf *b);
#undef Buf

} /* extern "C" */

/* ---- Diagnostic callback ---- */

struct DiagCtx {
    CompilerWorker *worker;
};

static void diag_cb(const char *msg, void *ctx)
{
    auto *dc = static_cast<DiagCtx *>(ctx);
    emit dc->worker->outputReady(QString::fromUtf8(msg));
}

CompilerWorker::CompilerWorker(QObject *parent)
    : QObject(parent)
{
}

/* ---- Embedded bas2wasm compilation ---- */

bool CompilerWorker::compileBasEmbedded(const QString &inputPath, const QString &outPath)
{
    QFile f(inputPath);
    if (!f.open(QIODevice::ReadOnly)) {
        emit error("Cannot open " + inputPath);
        return false;
    }
    QByteArray src = f.readAll();
    f.close();

    emit outputReady("[bas2wasm] compiling " + QFileInfo(inputPath).fileName() + "...\n");

    DiagCtx ctx = { this };
    bw_on_error = diag_cb;
    bw_on_info  = diag_cb;
    bw_cb_ctx   = &ctx;

    if (setjmp(bw_bail) != 0) {
        bas2wasm_reset();
        return false;
    }

    bw_Buf result = bas2wasm_compile_buffer(src.constData(), src.size());
    int nbytes = result.len;
    bas2wasm_reset();

    if (nbytes == 0) {
        bw_buf_free(&result);
        return false;
    }

    QFile out(outPath);
    if (!out.open(QIODevice::WriteOnly)) {
        bw_buf_free(&result);
        emit error("Cannot write " + outPath);
        return false;
    }
    out.write(reinterpret_cast<const char *>(result.data), result.len);
    out.close();
    bw_buf_free(&result);

    emit outputReady(QString("Wrote %1 bytes to %2\n").arg(nbytes).arg(outPath));
    return true;
}

/* ---- Embedded c2wasm compilation ---- */

bool CompilerWorker::compileCEmbedded(const QString &inputPath, const QString &outPath)
{
    QFile f(inputPath);
    if (!f.open(QIODevice::ReadOnly)) {
        emit error("Cannot open " + inputPath);
        return false;
    }
    QByteArray src = f.readAll();
    f.close();

    QString filename = QFileInfo(inputPath).fileName();
    emit outputReady("[c2wasm] compiling " + filename + "...\n");

    DiagCtx ctx = { this };
    cw_on_error = diag_cb;
    cw_on_info  = diag_cb;
    cw_cb_ctx   = &ctx;

    if (setjmp(cw_bail) != 0) {
        c2wasm_reset();
        return false;
    }

    QByteArray fname = filename.toUtf8();
    cw_Buf result = c2wasm_compile_buffer(src.constData(), src.size(), fname.constData());
    int nbytes = result.len;
    c2wasm_reset();

    if (nbytes == 0) {
        cw_buf_free(&result);
        return false;
    }

    QFile out(outPath);
    if (!out.open(QIODevice::WriteOnly)) {
        cw_buf_free(&result);
        emit error("Cannot write " + outPath);
        return false;
    }
    out.write(reinterpret_cast<const char *>(result.data), result.len);
    out.close();
    cw_buf_free(&result);

    emit outputReady(QString("Wrote %1 bytes to %2\n").arg(nbytes).arg(outPath));
    return true;
}

/* ---- Public compile dispatcher ---- */

void CompilerWorker::compile(const QString &inputPath)
{
    QFileInfo fi(inputPath);
    QString ext = fi.suffix().toLower();

    QString tmpWasm = "/tmp/conez_sim_" + fi.baseName() + ".wasm";
    m_tempWasm = tmpWasm;

    if (ext == "bas") {
        if (compileBasEmbedded(inputPath, tmpWasm))
            emit compiled(tmpWasm);
        else
            emit error("bas2wasm compilation failed");
    } else if (ext == "c") {
        if (compileCEmbedded(inputPath, tmpWasm))
            emit compiled(tmpWasm);
        else
            emit error("c2wasm compilation failed");
    } else if (ext == "wasm") {
        emit compiled(inputPath);
    } else {
        emit error("Unknown file type: " + ext);
    }
}
