#include "graphmaker.h"
#include <geogram/delaunay/delaunay.h>
#include <geogram/basic/command_line.h>
#include <geogram/basic/command_line_args.h>
#include <geogram/basic/logger.h>
#include <geogram/numerics/predicates.h>
#include <geogram/mesh/mesh.h>
#include <geogram/mesh/mesh_io.h>
#include <geogram_gfx/basic/GLSL.h>
#include <geogram_gfx/basic/GL.h>
#include <cmath>
#include "Graph.h"

using namespace GEO;
/*
 *
 * Faire une map qui mappe une couleur à un point pour le parcours de graph, pour vérifier qu'on voit tous les points, et dans le bon ordre.
 * (premier temps : map globale)
 *
 *
 */



namespace GraphMaker {

std::map <int, bool> inOutMap;
std::map <int, int> searchOrderMap;

typedef GEO::vector<GEO::vec2> Polygon;

GEO::Delaunay_var delaunay; //the delaunay polygon


void initialize() {


    GEO::initialize();
    GEO::Logger::instance()->set_quiet(false);

    GEO::CmdLine::import_arg_group("standard");
    GEO::CmdLine::import_arg_group("algo");
    GEO::CmdLine::import_arg_group("gfx");

    GEO::CmdLine::set_arg("sys:assert","abort");

    std::cout << "end of init" << std::endl;

    GEO::Graphics::initialize();
}

GEO::vec2 circumcenter(index_t t) {

    signed_index_t v1 = delaunay->cell_to_v()[3*t];
    signed_index_t v2 = delaunay->cell_to_v()[3*t+1];
    signed_index_t v3 = delaunay->cell_to_v()[3*t+2];
    vec2 p1(delaunay->vertex_ptr(index_t(v1)));
    vec2 p2(delaunay->vertex_ptr(index_t(v2)));
    vec2 p3(delaunay->vertex_ptr(index_t(v3)));
    return Geom::triangle_circumcenter(p1,p2,p3);
}

float distance(GEO::vec2 &p1,GEO::vec2 &p2) {
    float res=0;
    for (int i=0;i<2;++i) {
        res += (p1[i]-p2[i])*(p1[i]-p2[i]);
    }
    return res;
}

std::unique_ptr<Graph> makeMorePoints(Graph &inputGraph, float step) {
    /*
     * [DONE]
     * Faire un set pour tester si un edge est déjà parcourru : stocker les edges en (i,j) avec i<j
     * std::set<std::pair<int, int>> s
     * s.find(std::make_pair(i,j)) != s.end()
     * utiliser s.emplace(i,j) évite les recopies
     */

    std::set<std::pair<int, int>> visitedEdges;
    auto out = std::make_unique<Graph>();
    int currentVertex = 0;
    int nextVertex = inputGraph.directAdjacency(currentVertex)[0];

    //We compute the distance we went from the point on border, then we compare it to the distance to the point adjacent to this one.
    //Since we go in the same direction, if the distance squared is greater, then we went too far and must change vertex.


    while (visitedEdges.find(std::make_pair(currentVertex, nextVertex)) == visitedEdges.end()) {

        //Three cases : if we are in the first iteration of the point, we simply add the point the the new graph

        out -> addPoint(inputGraph.getPointCoordinate(currentVertex));
        float distFromConnectedVertex = distance(inputGraph.getPointCoordinate(currentVertex), inputGraph.getPointCoordinate(nextVertex));
        GEO::vec2 direction = (inputGraph.getPointCoordinate(nextVertex) - inputGraph.getPointCoordinate(currentVertex));

        //If we moved, we check the distance to the current point, to see if we aren't farther away than needed

        int nStep = 15; //TODO:20.03 ajuster le nStep pour mettre une densité linéique de points

        for (int k=1;k<nStep;++k) {
            out -> addPoint(inputGraph.getPointCoordinate(currentVertex) + direction*((float) k / (float) nStep));

        }
        //If we are too far, we change point. Since we need to always turn in the same direction, and points are disorganized, we need to check which direction
        // is the right one, of course for the first point, the direction will be random.

        visitedEdges.emplace(currentVertex, nextVertex);
        visitedEdges.emplace(nextVertex, currentVertex);

        if (visitedEdges.find(std::make_pair( nextVertex, inputGraph.directAdjacency(nextVertex)[0])) == visitedEdges.end()) {
            //Change of points
            currentVertex = nextVertex;
            nextVertex = inputGraph.directAdjacency(nextVertex)[0];
            //Refresh the data used

        }
        //Same here.
        else if (visitedEdges.find(std::make_pair( nextVertex, inputGraph.directAdjacency(nextVertex)[1])) == visitedEdges.end()) {
            currentVertex = nextVertex;
            nextVertex = inputGraph.directAdjacency(nextVertex)[1];
        }


    }
    for (int i=0;i<out -> getSize()-1;++i) {
        out -> addEdge({i,i+1});
    }
    out -> addEdge({out->getSize()-1,0});

    return std::move(out);

}

bool isOut( const Graph& graph, index_t t, index_t e) {
    index_t lv1 = (e+1)%3;
    index_t lv2 = (e+2)%3;
    index_t v1 = index_t(delaunay->cell_to_v()[3*t+lv1]);
    index_t v2 = index_t(delaunay->cell_to_v()[3*t+lv2]);

    return !(graph.existsEdge(v1,v2));

}

/** Param :
 * [OUT] An empty graph that will contain the voronoi graph
 * [IN] the step (in arbitrary units, in function of the chosen unit in the Graph) of the cutting of the Graph, in order ot make it closer to a graph of segment.
 *
 */
std::unique_ptr<Graph> extractVoronoi(Graph &inputGraph_small, float step) {
//    std::unique_ptr <Graph> voronoiGraph ;
    //Then, we call the function makeMorePoints to add the necessary points to be closest to the segment voronoi diagram.

    auto ptrGraph = makeMorePoints(inputGraph_small, step);
    Graph& inputGraph = *ptrGraph;
//    Graph inputGraph = *makeMorePoints(inputGraph_small, step);


    //return makeMorePoints(inputGraph_small, step);

    auto voronoiGraph = std::make_unique<Graph>();
    delaunay = Delaunay::create(2,"BDEL2d");
    delaunay->set_vertices(inputGraph.getPoints().size(), &(inputGraph.getPoints().data()->x));
    //CZ : delaunay is a smart pointer, overator-> allow to have access to the dereferenced inner pointer
    //CZ : set_vertices actually recompute the cells !
    // data is the pointer to the dynamic array stored inside the std::vector
    // we can note that once we have done
    //      &points.data()->x
    // we only obtain a pointer to a double (we do not know anymore about the vec2 class !
    // basically this is only valid if the storage for the vec2 class only contains the memory of two doubles and nothing more
    // (which is the case here, this would not be anymore the case if the class vec2 was containing other attribut or was virtual)
    //CZ : we only provide the number of points and not the number of coordinates as delaunay is actually a Delaunay2D.


    //We add the points in the Voronoi Graph
    for(index_t t=0; t<delaunay->nb_cells(); ++t) {
        voronoiGraph -> addPoint(circumcenter(t));
        //Index in graph are the same as inb cell indexes in voronoi
    }
    //Then, we add the link corresponding to each cell

    for(index_t t=0; t<delaunay->nb_cells(); ++t) {
        //function from display_edges
        for(index_t e=0; e<3; ++e) {
            signed_index_t t2 = delaunay->cell_to_cell()[3*t+e];
            if(t2 == -1) {
                voronoiGraph -> addInfinite(t); //add to the infinite matrix the vertex connected to the inf vertex.
                //TODO: évaluer quel edge rencontre un edge du graph donné en input et le stocker
                if (isOut(inputGraph,t,e)) {
                    voronoiGraph -> changeStatus(t,treatment::outside);
                }
                else {
                    voronoiGraph -> changeStatus(t,treatment::inside);
                }

            } else if(t2 >signed_index_t(t)) {

                voronoiGraph ->addEdge({(int) t,(int)t2});
            }
        }
    }
    return std::move(voronoiGraph);
}


std::vector <bool> insideOutTable(Graph &inputGraph) {

}

std::unique_ptr<Graph> removeOutsidePoints(Graph &voronoiGraph) {

}



void getVoronoiCell(index_t t0, index_t lv, Polygon& cell) {

    cell.resize(0);
    index_t v = index_t(delaunay->cell_to_v()[3*t0+lv]);
    bool on_border = false;
    index_t t = t0;

    // First, we turn around the vertex v. To do that, we compute
    // lv, the local index of v in the current triangle. Following
    // the standard numerotation of a triangle, edge number lv is
    // not incident to vertex v. The two other edges (lv+1)%3 and
    // (lv+2)%3 of the triangle are indicent to vertex v. By always
    // traversing (lv+1)%3, we turn around vertex v.
    do {
        index_t e = (lv+1)%3;
        signed_index_t neigh_t = delaunay->cell_to_cell()[3*t+e];
        if(neigh_t == -1) {
            on_border = true;
            break;
        }
        cell.push_back(circumcenter(t));
        t = index_t(neigh_t);
        lv = findVertex(t,v);
    } while(t != t0);


    // If one traversed edge is on the border of the convex hull, then
    // we empty the cell, and start turing around the vertex in the other
    // direction, i.e. by traversing this time edge (lv+2)%3 until we
    // reach the other edge on the border of the convex hull that is
    // incident to v.
    if(on_border) {
        cell.resize(0);
        cell.push_back(infiniteVertex(t,(lv + 1)%3));
        for(;;) {
            cell.push_back(circumcenter(t));
            index_t e = (lv+2)%3;
            signed_index_t neigh_t = delaunay->cell_to_cell()[3*t+e];
            if(neigh_t == -1) {
                cell.push_back(infiniteVertex(t, e));
                break;
            }
            t = index_t(neigh_t);
            lv = findVertex(t,v);
        }
    }

    Polygon clipped;
    //convex_clip_polygon(cell, border, clipped);
    cell.swap(clipped);
}

index_t findVertex(index_t t, index_t v) {
    for(index_t lv=0; lv<3; ++lv) {
        if(index_t(delaunay->cell_to_v()[3*t+lv]) == v) {
            return lv;
        }
    }
    geo_assert_not_reached;
}

/**
 * Standard graph depth search, works on cells of the voronoi diagram
 */
void graphSearchVoronoiCompute(Graph &inputGraph /*, FUNCTION AS ARGUMENT */ ) {
    extractVoronoi(inputGraph);
    std::vector<bool> v_visited(delaunay->nb_vertices());
    Polygon cell;
    for(index_t t=0; t<delaunay->nb_cells(); ++t) {
        for(index_t lv=0; lv<3; ++lv) {
            index_t v = index_t(delaunay->cell_to_v()[3*t+lv]);
            if(!v_visited[v]) {

                v_visited[v] = true;
                //obtain the cell
                getVoronoiCell(t,lv,cell);
                for(index_t i=1; i+1<cell.size(); ++i) {
                    /* EXECUTE FUNCTION (on each point of a given cell) */
                }
            }
        }
    }
    //every vertex was visited
}


/**
 * @brief standardDepthFirstSearch_AUX
 * @param inputGraph
 * @param v [the current computed point]
 * @param visited [Vector of visited points]
 * @param processFct applies fct to every point, use identity if you don't want one
 */

template<typename TLambda>
void standardDepthFirstSearch_AUX (Graph &inputGraph, int v, std::vector<bool> visited, TLambda &&processFct) {
    visited[v] = true;
    for(const auto &p :  inputGraph.directAdjacency(v)) {
        processFct(p);
        standardDepthFirstSearch_AUX(inputGraph,p,visited, processFct);

    }
}

/**
 * @brief standardDepthFirstSearch
 * @param inputGraph
 * @param firstVertex
 *
 * Uses auxiliary function
 *
 */

template<typename TLambda>
void standardDepthFirstSearch(Graph &inputGraph, int firstVertex, TLambda &&processFct)
{
    int size = inputGraph.getSize();
    std::vector<bool> visited(size);
    for (int i = 0; i < size ; i++)
        visited[i] = false;

    standardDepthFirstSearch_AUX(inputGraph, firstVertex , visited, processFct);
}


vec2 infiniteVertex(index_t t, index_t e) {
    index_t lv1 = (e+1)%3;
    index_t lv2 = (e+2)%3;
    index_t v1 = index_t(delaunay->cell_to_v()[3*t+lv1]);
    index_t v2 = index_t(delaunay->cell_to_v()[3*t+lv2]);
    vec2 p1(delaunay->vertex_ptr(v1));
    vec2 p2(delaunay->vertex_ptr(v2));
    vec2 n = normalize(p2-p1);
    n = vec2(n.y, -n.x);
    return 0.5*(p1+p2)+100000.0*n;

}




//    std::vector<bool> v_visited(inputGraph.getSize());







} //End namespace GraphMaker
