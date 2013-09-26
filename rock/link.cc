/// \file link.cc Links together roots with common descendants
#include "sample.h"
#include "volume-operation.h"
#include "thread.h"
#include "crop.h"

struct Family : array<uint64> {
    Family(uint64 root):root(root){}
    uint64 root;
};

/// Parses a 3D position tuple
int3 parsePosition(TextData& s) {
    uint x=s.integer(); s.whileAny(", "_);
    uint y=s.integer(); s.whileAny(", "_);
    uint z=s.integer(); s.whileAny(", "_);
    return int3(x,y,z);
}

/// Converts text file formatted as ((x y z):( x y z)+)*\n to families
array<Family> parseFamilies(const string& data) {
    array<Family> families;
    TextData s(data);
    while(s) {
        s.skip("("_); Family family (zOrder(parsePosition(s))); s.skip("):\n"_); s.skip();
        while(s && s.peek()!='(') { family << zOrder(parsePosition(s)); s.skip(); }
        //if(!family.size) continue; // Ignores single member families
        families << move(family);
    }
    return families;
}

struct Node {
    Node(uint64 origin):origin(origin){}
    uint64 origin;
    array<uint64> edges;
};

/// Links together roots with common descendants
array<Node> link(const array<Family>& families) {
    array<Node> nodes (families.size);
    for(const Family& A : families) nodes << A.root;
    for(uint i: range(families.size)) { const Family& A = families[i];
        for(uint j: range(families.size)) { const Family& B = families[j]; //FIXME: O(N²)
            if(i==j) continue;
            if(nodes[i].edges.contains(B.root)) continue;
            for(uint64 a: A) {
                for(uint64 b: B) {
                    if(a==b) goto link;
                }
            } /*else*/ continue; // no link between both families
            link: nodes[i].edges << B.root; nodes[j].edges << A.root;
        }
    }
    return nodes;
}

/// Converts nodes to a text file formatted as ((x y z):( x y z)+\n)*
String toASCII(const array<Node>& nodes) {
    String text; //FIXME: reserve
    for(const Node& node: nodes) {
        if(!node.edges) continue;
        int3 p = zOrder(node.origin); // Convert back Z-order index to position
        text << dec(p.x,3) << ' ' << dec(p.y,3) << ' ' << dec(p.z,3)+": "_; // Use same syntax for both origin and target (allow to easily check links when searching manually in the text data)
        for(uint64 target: node.edges) { int3 p = zOrder(target); text << dec(p.x,3) << ' ' << dec(p.y,3) << ' ' << dec(p.z,3) << "  "_; }
        text << "\n"_;
    }
    return text;
}

/// Links together roots with common descendants
class(Link, Operation) {
    virtual void execute(const Dict&, const ref<Result*>& outputs, const ref<Result*>& inputs) override {
        array<Node> nodes = link(parseFamilies(inputs[0]->data));
        outputs[0]->metadata = String("nodes"_);
        outputs[0]->data = toASCII(nodes);
    }
};
