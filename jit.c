/**********************************************************************

  jit.c -

  $Author$
  created at: Sun Jul 5 20:00:00 GMT 2015

  Copyright (C) 2015 Ben Segovia

**********************************************************************/

#include <llvm-c/IRReader.h>
#include <llvm-c/Target.h>
#include <llvm-c/ExecutionEngine.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

typedef struct jit_llvm_context {
    LLVMContextRef context;
    LLVMExecutionEngineRef jit;
    LLVMModuleRef module;
    LLVMTargetMachineRef tm;
} jit_llvm_context_t;

static LLVMModuleRef jit_llvm_context_load_module(jit_llvm_context_t *ctx, const char *file_name)
{
    LLVMMemoryBufferRef buffer = NULL;
    LLVMModuleRef module = NULL;
    char *message = NULL;
    FILE *bitcode = NULL;
    char *input = NULL;
    size_t sz = 0;

    /* open the bitcode with all the pre-built functions and types */
    bitcode = fopen(file_name, "rb");
    if (NULL == bitcode)
        goto error;
    fseek(bitcode, 0L, SEEK_END);
    sz = ftell(bitcode);
    fseek(bitcode, 0L, SEEK_SET);
    input = (char*) malloc(sz);
    if (fread(input, sz, 1, bitcode) != 1)
        goto error;

    /* load the pre-built module containing predefined types and functions */
    if (NULL == (buffer = LLVMCreateMemoryBufferWithMemoryRangeCopy(input, sz, "bitcode_buffer")))
        goto error;
    if (LLVMParseIRInContext(ctx->context, buffer, &module, &message)) {
        fprintf(stderr, "jit: error loading pre-built module: %s\n", message);
        goto error;
    }

exit:
    if (NULL != message)
        LLVMDisposeMessage(message);
    if (NULL != input)
        free(input);
    if (NULL != bitcode)
        fclose(bitcode);
    return module;
error:
    if (NULL != module) {
        LLVMDisposeModule(module);
        module = NULL;
    }
    goto exit;
}

static int jit_llvm_init(void)
{
    if (LLVMInitializeNativeTarget())
        return 1;
    if (LLVMInitializeNativeAsmParser())
        return 1;
    if (LLVMInitializeNativeAsmPrinter())
        return 1;
    if (LLVMInitializeNativeDisassembler())
        return 1;
    LLVMLinkInMCJIT();
    return 0;
}

static void jit_llvm_context_delete(jit_llvm_context_t *ctx)
{
    if (NULL == ctx)
        return;
    if (NULL != ctx->module)
        LLVMDisposeModule(ctx->module);
    if (NULL != ctx->context)
        LLVMContextDispose(ctx->context);

}

static jit_llvm_context_t *jit_llvm_context_new(void)
{
    struct LLVMMCJITCompilerOptions options = {0};
    jit_llvm_context_t *ctx = NULL;
    char *msg = NULL;

    ctx = (jit_llvm_context_t *) calloc(1, sizeof(jit_llvm_context_t));
    ctx->context = LLVMContextCreate();
    if (NULL == (ctx->module = jit_llvm_context_load_module(ctx, "d.bc"))) {
        fprintf(stderr, "jit: unable to load prebuilt modulen");
        goto error;
    }

    options.OptLevel = 3;
    LLVMInitializeMCJITCompilerOptions(&options, sizeof(options));
    if (LLVMCreateMCJITCompilerForModule(&ctx->jit, ctx->module, &options, sizeof(options), &msg)) {
        fprintf(stderr, "failed to initialize MCJIT execution engine: %s\n", msg ? msg : "");
        goto error;
    }
    ctx-> tm = LLVMGetExecutionEngineTargetMachine(ctx->jit);
    LLVMSetTargetMachineAsmVerbosity(ctx->tm, 1);

exit:
    if (NULL != msg)
        LLVMDisposeMessage(msg);
    return ctx;
error:
    jit_llvm_context_delete(ctx);
    ctx = NULL;
    goto exit;
}

static jit_llvm_context_t *llvm_ctx = NULL;
void rb_jit_init()
{
    if (jit_llvm_init()) {
        fprintf(stderr, "jit: unable to initialize LLVM\n");
        exit(EXIT_FAILURE);
    }
    if (NULL == (llvm_ctx = jit_llvm_context_new())) {
        fprintf(stderr, "jit: unable to create LLVM context\n");
        exit(EXIT_FAILURE);
    }
}

void rb_jit_destroy()
{
    jit_llvm_context_delete(llvm_ctx);
}

#if 0
typedef void (*hop_ptr)();
int jit_main()
{
    jit_llvm_context_t *ctx = NULL;
    LLVMValueRef fn = NULL;
    union {hop_ptr fn; void *ptr;} hop;
    struct timeval stv, etv;
    struct timezone stz, etz;

    if (jit_llvm_init()) {
        fprintf(stderr, "jit: unable to initialize LLVM\n");
        return 1;
    }
    if (NULL == (ctx = jit_llvm_context_new())) {
        fprintf(stderr, "jit: unable to create LLVM context\n");
        return 1;
    }

    if (LLVMFindFunction(ctx->jit, "run", &fn)) {
        fprintf(stderr, "jit: unable to find function 'run'\n");
        return 1;
    }

    /* compile and get the function */
    gettimeofday(&stv, &stz);
    hop.ptr = LLVMGetPointerToGlobal(ctx->jit, fn);
    gettimeofday(&etv, &etz);
    fprintf(stderr, "jit: time to compile: %ld ms\n",
            1000 * (etv.tv_sec - stv.tv_sec) + (etv.tv_usec - stv.tv_usec) / 1000);

    hop.fn();

    /* free up everything */
    jit_llvm_context_delete(ctx);
    return 0;
}
#endif

