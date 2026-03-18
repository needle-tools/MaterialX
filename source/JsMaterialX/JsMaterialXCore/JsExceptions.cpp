//
// Copyright Contributors to the MaterialX Project
// SPDX-License-Identifier: Apache-2.0
//

#include <MaterialXRender/ShaderRenderer.h>
#include <MaterialXGenShader/ShaderGenerator.h>
#include <MaterialXFormat/XmlIo.h>

#include <emscripten/bind.h>

#include <exception>
#include <string>

namespace ems = emscripten;
namespace mx = MaterialX;

namespace jsexceptions
{
std::string getExceptionMessage(ems::val exceptionLike)
{
    try
    {
        const std::string type = exceptionLike.typeOf().as<std::string>();
        if (type == "number")
        {
            // Legacy: numeric pointer to std::exception
            int exceptionPtr = exceptionLike.as<int>();
            return std::string(reinterpret_cast<std::exception *>(exceptionPtr)->what());
        }
        if (type == "string")
        {
            return exceptionLike.as<std::string>();
        }
        if (type == "object")
        {
            // Try typical Error-like object with a message property
            bool hasMessage = false;
            try { hasMessage = exceptionLike.call<bool>("hasOwnProperty", std::string("message")); } catch (...) {}
            if (hasMessage)
            {
                return exceptionLike["message"].as<std::string>();
            }
            // Fallback to toString()
            try { return exceptionLike.call<std::string>("toString"); } catch (...) {}
        }
    }
    catch (...)
    {
        // Fall through to default
    }
    return std::string("Unknown exception");
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
