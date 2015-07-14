#ifndef TE_ENTITY_MANAGER_H
#define TE_ENTITY_MANAGER_H

#include <vector>
#include <deque>

namespace te
{
    class Entity
    {
    public:
        bool operator<(const Entity&) const;
        bool operator==(const Entity&) const;
        bool operator!=(const Entity&) const;
    private:
        friend class EntityManager;
        unsigned index;
        unsigned generation;
    };

    class EntityManager
    {
    public:
        EntityManager(unsigned size = 1024);
        Entity create();
        bool isAlive(Entity e) const;
        void destroy(Entity e);
    private:
        typedef std::vector<Entity> EntityContainer;
        typedef std::deque<unsigned> IndexQueue;

        EntityContainer mEntities;
        IndexQueue mAvailableIndices;
        unsigned mAllocationSize;
    };
}

#endif /* TE_ENTITY_H */
