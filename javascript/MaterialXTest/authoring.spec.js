import { expect } from 'chai';
import Module from './_build/JsMaterialXCore.js';

describe('Mutable authoring API', () =>
{
    let mx;

    before(async () =>
    {
        mx = await Module();
    });

    function expectValid(doc)
    {
        const message = {};
        expect(doc.validate(message), message.message).to.be.true;
    }

    function buildDefinitionDocument()
    {
        const doc = mx.createDocument();

        const nodeDef = doc.addNodeDef('ND_editor_tint', 'color3', 'editor_tint');
        nodeDef.setNodeGroup('texture2d');
        nodeDef.setAttribute('editor:custom', 'preserved');

        const nodeGraph = doc.addNodeGraph('NG_editor_tint');
        nodeGraph.setNodeDef(nodeDef);
        nodeGraph.setAttribute('editor:graph', 'custom');
        const constant = nodeGraph.addNode('constant', 'tint_constant', 'color3');
        const value = constant.addInput('value', 'color3');
        const tintColor = new mx.Color3(0.2, 0.4, 0.9);
        value.setValueColor3(tintColor);
        tintColor.delete();
        const tint = nodeGraph.addInterfaceName('tint_constant/value', 'tint');
        const output = nodeGraph.addOutput('out', 'color3');
        output.setNodeName(constant.getName());

        const attrDef = doc.addAttributeDef('AD_editor_show_all_inputs');
        attrDef.setAttrName('editor:show_all_inputs');
        attrDef.setType('boolean');
        attrDef.setValueBoolean(true);

        return { doc, nodeDef, nodeGraph, tint, constant, value, output, attrDef };
    }

    it('creates, mutates, connects, validates, and serializes structural document elements', () =>
    {
        const { doc, nodeDef, nodeGraph, tint, constant, value, output, attrDef } = buildDefinitionDocument();

        expect(nodeDef.getAttribute('editor:custom')).to.equal('preserved');
        nodeDef.removeAttribute('editor:custom');
        expect(nodeDef.hasAttribute('editor:custom')).to.be.false;
        nodeDef.setAttribute('editor:custom', 'restored');

        value.setNodeGraphString(nodeGraph.getName());
        value.setOutputString(output.getName());
        expect(value.getNodeGraphString()).to.equal('NG_editor_tint');
        expect(value.getOutputString()).to.equal('out');
        const connectionEditXml = mx.writeToXmlString(doc);
        expect(connectionEditXml).to.contain('nodegraph="NG_editor_tint"');
        expect(connectionEditXml).to.contain('output="out"');
        value.removeAttribute('nodegraph');
        value.removeAttribute('output');

        output.setNodeName(constant.getName());
        expect(output.getNodeName()).to.equal('tint_constant');
        expect(output.getUpstreamElement().equals(constant)).to.be.true;

        expect(nodeGraph.getNodeDef().equals(nodeDef)).to.be.true;
        nodeGraph.setNodeDef(null);
        expect(nodeGraph.hasNodeDefString()).to.be.false;
        nodeGraph.setNodeDef(nodeDef);
        expect(nodeGraph.getNodeDef().equals(nodeDef)).to.be.true;

        expectValid(doc);

        const xml = mx.writeToXmlString(doc);
        expect(xml).to.contain('editor:custom="restored"');
        expect(xml).to.contain('interfacename="tint"');
        expect(xml).to.contain('attributedef');

        doc.removeAttributeDef(attrDef.getName());
        doc.removeNodeGraph(nodeGraph.getName());
        doc.removeNodeDef(nodeDef.getName());
        expect(doc.getAttributeDefs()).to.have.length(0);
        expect(doc.getNodeGraphs()).to.have.length(0);
        expect(doc.getNodeDefs()).to.have.length(0);

        tint.delete();
        value.delete();
        output.delete();
        constant.delete();
        attrDef.delete();
        nodeGraph.delete();
        nodeDef.delete();
        doc.delete();
    });

    it('copies and moves definitions between documents while preserving source URIs for XInclude serialization', () =>
    {
        const { doc, nodeDef, nodeGraph } = buildDefinitionDocument();
        const includeDoc = mx.createDocument();
        const rootDoc = mx.createDocument();

        const movedGraph = mx.moveElementToParent(nodeGraph, includeDoc);
        expect(doc.getNodeGraph('NG_editor_tint')).to.equal(null);
        expect(includeDoc.getNodeGraph('NG_editor_tint').equals(movedGraph)).to.be.true;

        const copiedNodeDef = mx.copyElementToParent(nodeDef, includeDoc, 'ND_editor_tint_copy');
        copiedNodeDef.setNodeString('editor_tint_copy');
        movedGraph.setNodeDef(copiedNodeDef);

        mx.setSourceUriRecursive(movedGraph, 'includes/editor_tint.mtlx');
        mx.setSourceUriRecursive(copiedNodeDef, 'includes/editor_tint.mtlx');
        expect(movedGraph.getActiveSourceUri()).to.equal('includes/editor_tint.mtlx');
        expect(movedGraph.getOutput('out').getActiveSourceUri()).to.equal('includes/editor_tint.mtlx');

        const includedGraphRef = mx.copyElementToParent(movedGraph, rootDoc);
        const includedNodeDefRef = mx.copyElementToParent(copiedNodeDef, rootDoc);
        expect(includedGraphRef.getSourceUri()).to.equal('includes/editor_tint.mtlx');
        expect(includedNodeDefRef.getSourceUri()).to.equal('includes/editor_tint.mtlx');

        const writeOptions = new mx.XmlWriteOptions();
        writeOptions.writeXIncludeEnable = true;
        const rootXml = mx.writeToXmlString(rootDoc, writeOptions);
        expect(rootXml).to.contain('<xi:include href="includes/editor_tint.mtlx"');
        expect(rootXml).not.to.contain('<nodegraph name="NG_editor_tint"');
        expect(rootDoc.getReferencedSourceUris()).to.include('includes/editor_tint.mtlx');

        mx.clearSourceUriRecursive(movedGraph);
        expect(movedGraph.hasSourceUri()).to.be.false;
        expect(movedGraph.getOutput('out').hasSourceUri()).to.be.false;

        expectValid(includeDoc);
        expectValid(rootDoc);

        writeOptions.delete();
        copiedNodeDef.delete();
        movedGraph.delete();
        nodeDef.delete();
        includeDoc.delete();
        rootDoc.delete();
        doc.delete();
    });
});
