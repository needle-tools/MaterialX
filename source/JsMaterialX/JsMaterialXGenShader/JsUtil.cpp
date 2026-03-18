//
// Copyright Contributors to the MaterialX Project
// SPDX-License-Identifier: Apache-2.0
//

#include <JsMaterialX/Helpers.h>
#include <MaterialXCore/Element.h>
#include <MaterialXGenShader/Shader.h>
#include <MaterialXGenShader/Util.h>

#include <string>
#include <emscripten/bind.h>

namespace ems = emscripten;
namespace mx = MaterialX;

/// Returns the first renderable element from the given document. This element can be used to generate a shader.
mx::ElementPtr findRenderableElement(mx::DocumentPtr doc)
{
    mx::StringVec renderablePaths;
    std::vector<mx::TypedElementPtr> elems = mx::findRenderableElements(doc);

    for (mx::TypedElementPtr elem : elems)
    {
        mx::TypedElementPtr renderableElem = elem;
        mx::NodePtr node = elem->asA<mx::Node>();
        if (node && node->getType() == mx::MATERIAL_TYPE_STRING)
        {
            std::vector<mx::NodePtr> shaderNodes = getShaderNodes(node, mx::SURFACE_SHADER_TYPE_STRING);
            if (!shaderNodes.empty())
            {
                renderableElem = *shaderNodes.begin();
            }
        }

        const auto& renderablePath = renderableElem->getNamePath();
        mx::ElementPtr renderableElement = doc->getDescendant(renderablePath);
        mx::TypedElementPtr typedElem = renderableElement ? renderableElement->asA<mx::TypedElement>() : nullptr;
        if (typedElem)
        {
            return renderableElement;
        }
    }

    return nullptr;
}

/// Returns the alpha mode for a renderable element as a string: "opaque", "mask", or "blend".
/// Also returns the alpha cutoff value for mask mode via the outCutoff parameter.
/// This inspects the shader node (and its nodegraph implementation) for alpha_mode inputs.
std::string getAlphaMode(mx::ElementPtr element, const std::string& target)
{
    mx::NodePtr shaderNode = mx::resolveShaderNode(element, target);
    if (!shaderNode)
        return "opaque";

    // Lambda to read alpha mode from a node's alpha_mode input
    auto readAlphaMode = [](mx::NodePtr node) -> std::string
    {
        mx::InputPtr alphaModeInput = node->getActiveInput("alpha_mode");
        if (!alphaModeInput)
            return "";
        mx::ValuePtr val = alphaModeInput->getValue();
        if (!val || !val->isA<int>())
            return "";
        int alphaMode = val->asA<int>();
        if (alphaMode == 0) return "opaque";
        if (alphaMode == 1) return "mask";
        if (alphaMode == 2) return "blend";
        return "";
    };

    // Check the shader node directly (e.g. top-level gltf_pbr)
    std::string result = readAlphaMode(shaderNode);
    if (!result.empty())
        return result;

    // Check inside the nodegraph implementation (e.g. custom shader graph wrapping gltf_pbr)
    mx::NodeDefPtr nodeDef = shaderNode->getNodeDef(target);
    mx::InterfaceElementPtr impl = nodeDef ? nodeDef->getImplementation(target) : nullptr;
    if (impl && impl->isA<mx::NodeGraph>())
    {
        mx::NodeGraphPtr graph = impl->asA<mx::NodeGraph>();
        for (mx::NodePtr implNode : graph->getNodes())
        {
            if (implNode->getType() != mx::SURFACE_SHADER_TYPE_STRING)
                continue;
            result = readAlphaMode(implNode);
            if (!result.empty())
                return result;
        }
    }

    // Fallback: use transparency detection
    bool isTransparent = mx::isTransparentSurface(element, target);
    return isTransparent ? "blend" : "opaque";
}

EMSCRIPTEN_BINDINGS(Util)
{
    BIND_FUNC("isTransparentSurface", mx::isTransparentSurface, 1, 2, mx::ElementPtr, const std::string&);

    ems::function("findRenderableElement", &findRenderableElement);
    ems::function("getAlphaMode", ems::optional_override([](mx::ElementPtr element, const std::string& target) {
        return getAlphaMode(element, target);
    }));
}
