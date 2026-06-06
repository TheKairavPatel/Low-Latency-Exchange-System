#include "../engine/engine.hpp"

class SnapshotWriter
{
    Engine& engine;
    public:
    SnapshotWriter(Engine& engine) : engine(engine) {}
    void writeSnapshot(const char* filename);
    void run();
};
