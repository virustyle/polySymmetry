/**
    Copyright (c) 2017 Ryan Porter    
    You may use, distribute, or modify this code under the terms of the MIT license.
*/

/*
The poly symmetry algorithm traverses the topology of a polygon mesh to 
calculate the symmetry of its components. It does this by examining pairs of 
edges that are known to be symmetrical, walking the vertices of the faces 
adjacent to the edges, establishing a symmetry table. 

Because an edge may have two adjacent faces, the user must provide starting 
data for the algorithm in the form of a pair of symmetrical faces, edges, 
and vertices. The edges must be on the faces, and the vertices must be on 
the edges. 

From this initial condition, we can assume that the other vertices 
on the edges are symmetrical. Once an edge, the the vertices on it, and one
of the faces adjacent to it have had their symmetry computed, the rest of the 
components on the shell can have their symmetry computed thus:

    > Start on an edge with known symmetry, whose vertices have known 
      symmetry, and is on a face with known symmetry. 
    > Get the other face connected to the edge - its symmetry is unknown.     
    > Starting at either vertex on the edge, walk to its other neighboring 
      vertex on the face. A neighboring vertex is a vertex on the same face 
      connected by an edge. On the edge that is symmetrical to this edge, 
      do the same. These neighboring vertices are symmetrical, as is the edge 
      that was traversed to find them. 
    > Repeat until all the vertices on the face have had their 
      symmetry computed. 
    > Repeat the algorithm, starting at each pair of traversed edges, until 
      the symmetry of all components on this shell are computed.
*/

// TODO - unintuitive results returned if the mesh does not have a center edge loop whose vertices are symmetrical to themselves.

#include "meshData.h"
#include "polySymmetry.h"
#include "selection.h"
#include "util.h"

#include <algorithm>
#include <queue>
#include <utility> 
#include <vector>

#include <maya/MDagPath.h>
#include <maya/MFnMesh.h>

using namespace std;


PolySymmetryData::PolySymmetryData() 
{
    MeshData meshData = MeshData();

    examinedEdges = vector<bool>();
    examinedFaces = vector<bool>();
    examinedVertices = vector<bool>();

    vertexSymmetryIndices = vector<int>();
    edgeSymmetryIndices = vector<int>();
    faceSymmetryIndices = vector<int>();

    vertexSides = vector<int>();
    edgeSides = vector<int>();
    faceSides = vector<int>();

    leftSideVertexIndices = vector<int>();
}


PolySymmetryData::~PolySymmetryData() 
{
    this->clear();
}


void PolySymmetryData::initialize(MDagPath &mesh) 
{
    meshData.unpackMesh(mesh);
    this->reset();
}


void PolySymmetryData::clear()
{
    meshData.clear();
    this->reset();
}


void PolySymmetryData::reset()
{
    examinedEdges.clear();
    examinedFaces.clear();
    examinedVertices.clear();

    edgeSymmetryIndices.clear();
    faceSymmetryIndices.clear();
    vertexSymmetryIndices.clear();

    edgeSides.clear();
    faceSides.clear();
    vertexSides.clear();

    leftSideVertexIndices.clear();

    examinedEdges.resize(meshData.numberOfEdges, false);
    examinedFaces.resize(meshData.numberOfFaces, false);
    examinedVertices.resize(meshData.numberOfVertices, false);

    edgeSymmetryIndices.resize(meshData.numberOfEdges, -1);
    faceSymmetryIndices.resize(meshData.numberOfFaces, -1);
    vertexSymmetryIndices.resize(meshData.numberOfVertices, -1);

    edgeSides.resize(meshData.numberOfEdges, -1);
    faceSides.resize(meshData.numberOfFaces, -1);
    vertexSides.resize(meshData.numberOfVertices, -1);
}


void PolySymmetryData::findSymmetricalVertices(ComponentSelection &selection)
{
    queue<pair<int, int>> symmetricalEdgesQueue = queue<pair<int, int>>();
    symmetricalEdgesQueue.push(selection.edgeIndices);

    if (selection.leftVertexIndex != -1) 
    {
        leftSideVertexIndices.push_back(selection.leftVertexIndex);
    }

    this->findFirstSymmetricalVertices(selection);

    while (!symmetricalEdgesQueue.empty())
    {
        pair<int, int> edgePair = symmetricalEdgesQueue.front();
        symmetricalEdgesQueue.pop();

        markSymmetricalEdges(edgePair.first, edgePair.second);

        pair<int, int> facesPair = getUnexaminedFaces(edgePair);

        if (facesPair.first == -1 || facesPair.second == -1)
        {
            continue;
        }

        markSymmetricalFaces(facesPair.first, facesPair.second);

        this->findSymmetricalVerticesOnFace(facesPair);
        this->findSymmetricalEdgesOnFace(symmetricalEdgesQueue, facesPair.first);
    }
}


void PolySymmetryData::findFirstSymmetricalVertices(ComponentSelection &selection)
{
    markSymmetricalVertices(selection.vertexIndices.first, selection.vertexIndices.second);
    markSymmetricalFaces(selection.faceIndices.first, selection.faceIndices.second);
    
    int vertex0 = meshData.edgeData[selection.edgeIndices.first].connectedVertices[0];
    int vertex1 = meshData.edgeData[selection.edgeIndices.first].connectedVertices[1];

    int nextVertex0 = examinedVertices[vertex0] ? vertex1 : vertex0;

    vertex0 = meshData.edgeData[selection.edgeIndices.second].connectedVertices[0];
    vertex1 = meshData.edgeData[selection.edgeIndices.second].connectedVertices[1];

    int nextVertex1 = examinedVertices[vertex0] ? vertex1 : vertex0;

    markSymmetricalVertices(nextVertex0, nextVertex1);

    this->findSymmetricalVerticesOnFace(selection.faceIndices);
}


pair<int, int> PolySymmetryData::getUnexaminedFaces(pair<int, int> &edgePair)
{
    vector<int> sharedFaces = intersection(
        meshData.edgeData[edgePair.first].connectedFaces,
        meshData.edgeData[edgePair.second].connectedFaces
    );

    int face0 = -1;
    int face1 = -1;

    if (sharedFaces.size() > 0)
    {
        for (int &faceIndex : sharedFaces)
        {
            if (this->examinedFaces[faceIndex])
            {
                continue;
            }

            if (face0 == -1)
            {
                face0 = faceIndex;
                continue;
            }

            if (face1 == -1)
            {
                face1 = faceIndex;
                continue;
            }
        }
    } else {
        face0 = getUnexaminedFace(edgePair.first);
        face1 = getUnexaminedFace(edgePair.second);
    }

    return pair<int, int>(face0, face1);
}


int PolySymmetryData::getUnexaminedFace(int &edgeIndex)
{
    int result = -1;

    for (int &faceIndex : meshData.edgeData[edgeIndex].connectedFaces)
    {
        if (!examinedFaces[faceIndex])
        {
            result = faceIndex;
            break;
        }
    }

    return result;
}


void PolySymmetryData::findSymmetricalVerticesOnFace(pair<int, int> &facePair)
{
    queue<int> faceVerticesQueue = queue<int>();
    
    for (int &v : meshData.faceData[facePair.first].connectedVertices)
    {
        if (examinedVertices[v])
        {
            faceVerticesQueue.push(v);
        }
    }

    while (!faceVerticesQueue.empty())
    {
        int vertex0 = faceVerticesQueue.front();
        faceVerticesQueue.pop();

        int vertex1 = vertexSymmetryIndices[vertex0];

        int nextVertex0 = getUnexaminedVertexSibling(vertex0, facePair.first);
        int nextVertex1 = getUnexaminedVertexSibling(vertex1, facePair.second);

        if (nextVertex0 == -1 || nextVertex1 == -1)
        {
            continue;
        }

        markSymmetricalVertices(nextVertex0, nextVertex1);

        faceVerticesQueue.push(nextVertex0);
    }
}


void PolySymmetryData::findSymmetricalEdgesOnFace(queue<pair<int, int>> &symmetricalEdgesQueue, int &faceIndex)
{        
    int vertex0;
    int vertex1;

    int edgeIndex;

    for (int &e : meshData.faceData[faceIndex].connectedEdges)
    {
        if (examinedEdges[e]) { continue; }

        vertex0 = vertexSymmetryIndices[meshData.edgeData[e].connectedVertices[0]];
        vertex1 = vertexSymmetryIndices[meshData.edgeData[e].connectedVertices[1]];

        if (vertex0 == -1 || vertex1 == -1)
        {
            continue;
        }
        
        vector<int> sharedEdges = intersection(
            meshData.vertexData[vertex0].connectedEdges, 
            meshData.vertexData[vertex1].connectedEdges
        );

        if (sharedEdges.size() != 1) { continue; }

        edgeIndex = sharedEdges[0];

        if (!examinedEdges[edgeIndex]) 
        { 
            symmetricalEdgesQueue.push(pair<int, int>(e, edgeIndex));   
        }

        markSymmetricalEdges(e, edgeIndex);
        
    }
}


int PolySymmetryData::getUnexaminedVertexSibling(int &vertexIndex, int &faceIndex)
{
    int result = -1;

    for (int &v : meshData.vertexData[vertexIndex].faceVertexSiblings[faceIndex])
    {
        if (examinedVertices[v]) 
        {
            continue;
        }

        result = v;
        break;
    }

    return result;
}


void PolySymmetryData::findVertexSides(vector<int> &leftSideVertexIndices)
{
    int LEFT = 1;
    int RIGHT = -1;
    int CENTER = 0;

    vector<int> visitedVertices(meshData.numberOfVertices, false);
    queue<int> nextVertexQueue = queue<int>();

    vertexSides.clear();
    vertexSides.resize(meshData.numberOfVertices, 0);

    for (int &i : leftSideVertexIndices)
    {
        nextVertexQueue.push(i);
        vertexSides[i] = LEFT;
    }

    int vertexIndex = -1;

    while (!nextVertexQueue.empty())
    {
        vertexIndex = nextVertexQueue.front();
        nextVertexQueue.pop();

        if (visitedVertices[vertexIndex]) { continue; }

        visitedVertices[vertexIndex] = true;

        if (vertexSymmetryIndices[vertexIndex] == vertexIndex)
        {
            vertexSides[vertexIndex] = CENTER;
        } else {
            vertexSides[vertexIndex] = LEFT;

            for (int &i : meshData.vertexData[vertexIndex].connectedVertices)
            {
                if (!visitedVertices[i] && vertexSymmetryIndices[i] != vertexIndex) 
                {
                    nextVertexQueue.push(i);
                }
            }
        }
    }

    for (int &i : leftSideVertexIndices)
    {
        nextVertexQueue.push(vertexSymmetryIndices[i]);
        vertexSides[vertexSymmetryIndices[i]] = RIGHT;
    }

    while (!nextVertexQueue.empty())
    {
        vertexIndex = nextVertexQueue.front();
        nextVertexQueue.pop();

        if (visitedVertices[vertexIndex]) { continue; }

        visitedVertices[vertexIndex] = true;

        if (vertexSymmetryIndices[vertexIndex] == vertexIndex)
        {
            vertexSides[vertexIndex] = CENTER;
        } else {
            vertexSides[vertexIndex] = RIGHT;

            for (int &i : meshData.vertexData[vertexIndex].connectedVertices)
            {
                if (!visitedVertices[i]) 
                {
                    nextVertexQueue.push(i);
                }
            }
        }
    }
}


void PolySymmetryData::markSymmetricalVertices(int &i0, int &i1)
{
    vertexSymmetryIndices[i0] = i1;
    vertexSymmetryIndices[i1] = i0;

    examinedVertices[i0] = true;
    examinedVertices[i1] = true;
}


void PolySymmetryData::markSymmetricalEdges(int &i0, int &i1)
{
    edgeSymmetryIndices[i0] = i1;
    edgeSymmetryIndices[i1] = i0;

    examinedEdges[i0] = true;
    examinedEdges[i1] = true;
}


void PolySymmetryData::markSymmetricalFaces(int &i0, int &i1)
{
    faceSymmetryIndices[i0] = i1;
    faceSymmetryIndices[i1] = i0;

    examinedFaces[i0] = true;
    examinedFaces[i1] = true;
}


void PolySymmetryData::finalizeSymmetry() 
{
    int LEFT = 1;
    int RIGHT = -1;
    int CENTER = 0;

    for (int i = 0; i < meshData.numberOfEdges; i++)
    {
        int sv0 = vertexSides[meshData.edgeData[i].connectedVertices[0]];
        int sv1 = vertexSides[meshData.edgeData[i].connectedVertices[1]];

        if (sv0 == CENTER && sv1 == CENTER)
        {
            edgeSides[i] = CENTER;
        } else if (sv0 == RIGHT || sv1 == RIGHT) {
            edgeSides[i] = RIGHT;
        } else if (sv0 == LEFT || sv1 == LEFT) {
            edgeSides[i] = LEFT;
        }
    }

    vector<int> faceVertexSides;

    for (int i = 0; i < meshData.numberOfFaces; i++)
    {
        faceVertexSides.resize(meshData.faceData[i].connectedVertices.size());

        for (int j = 0; j < meshData.faceData[i].connectedVertices.size(); j++)
        {
            faceVertexSides[j] = vertexSides[meshData.faceData[i].connectedVertices[j]];
        }

        bool onTheLeft = contains(faceVertexSides, LEFT);
        bool onTheRight = contains(faceVertexSides, RIGHT);

        faceSides[i] = (onTheLeft ? LEFT : CENTER) + (onTheRight ? RIGHT : CENTER);
    }
}