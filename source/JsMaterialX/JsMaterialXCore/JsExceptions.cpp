//
// Copyright Contributors to the MaterialX Project
// SPDX-License-Identifier: Apache-2.0
//

#include <MaterialXRender/ShaderRenderer.h>
#include <MaterialXGenShader/ShaderGenerator.h>
#include <MaterialXFormat/XmlIo.h>

#include <emscripten/bind.h>

#include <string>

namespace ems = emscripten;
namespace mx = MaterialX;

namespace jsexceptions
{
std::string getExceptionMessage(int exceptionPtr)
{
    return std::string(reinterpret_cast<std::exception *>(exceptionPtr)->what());
}

/// Returns a detailed error message from a C++ exception pointer.
/// For ExceptionRenderError, includes all entries from the error log.
/// For other exception types, returns the .what() message.
std::string getExceptionDetailedMessage(int exceptionPtr)
{
    auto* baseException = reinterpret_cast<std::exception*>(exceptionPtr);

    // Check for ExceptionRenderError which carries an error log
    auto* renderError = dynamic_cast<mx::ExceptionRenderError*>(baseException);
    if (renderError)
    {
        std::string result = renderError->what();
        const auto& errorLog = renderError->errorLog();
        if (!errorLog.empty())
        {
            result += "\n--- Error Log ---";
            for (const auto& entry : errorLog)
            {
                result += "\n" + entry;
            }
        }
        return result;
    }

    // For all other exceptions, return the basic message
    return std::string(baseException->what());
}
} // namespace jsexceptions

EMSCRIPTEN_BINDINGS(exceptions)
{
    ems::function("getExceptionMessage", &jsexceptions::getExceptionMessage);
    ems::function("getExceptionDetailedMessage", &jsexceptions::getExceptionDetailedMessage);
}
