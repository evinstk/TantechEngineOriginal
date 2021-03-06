#include "tmx.h"
#include "entity_manager.h"
#include "transform_component.h"
#include "animation_component.h"
#include "animation_factory.h"
#include "data_component.h"

#include <lua.hpp>
#include <LuaBridge.h>
#include <glm/gtx/transform.hpp>

#include <map>
#include <memory>
#include <functional>

namespace te
{
    static TMX::Tileset::Tile::ObjectGroup::Object::Shape getShapeEnum(const std::string& shapeStr)
    {
        static const std::map<std::string, TMX::Tileset::Tile::ObjectGroup::Object::Shape> shapeMap = {
            {"rectangle", TMX::Tileset::Tile::ObjectGroup::Object::Shape::RECTANGLE}
        };
        try {
            return shapeMap.at(shapeStr);
        }
        catch (std::out_of_range e) {
            throw std::runtime_error("Unsupported shape type.");
        }
    }

    static TMX::Layer::Type getLayerType(const std::string& layerStr)
    {
        static const std::map<std::string, TMX::Layer::Type> layerMap = {
            {"tilelayer", TMX::Layer::Type::TILELAYER},
            {"objectgroup", TMX::Layer::Type::OBJECTGROUP}
        };
        try {
            return layerMap.at(layerStr);
        }
        catch (std::out_of_range e) {
            throw std::runtime_error("Unsupported layer type.");
        }
    }

    static TMX::Orientation getOrientation(const std::string& orientationStr)
    {
        static const std::map<std::string, TMX::Orientation> orientationMap = {
            {"orthogonal", TMX::Orientation::ORTHOGONAL}
        };
        try {
            return orientationMap.at(orientationStr);
        }
        catch (std::out_of_range e) {
            throw std::runtime_error("Unsupported orientation.");
        }
    }

    static TMX::RenderOrder getRenderOrder(const std::string& renderOrderStr)
    {
        static const std::map<std::string, TMX::RenderOrder> renderOrderMap = {
            {"right-down", TMX::RenderOrder::RIGHT_DOWN}
        };
        try {
            return renderOrderMap.at(renderOrderStr);
        }
        catch (std::out_of_range e) {
            throw std::runtime_error("Unsupported render order.");
        }
    }

    static luabridge::LuaRef getTMXRef(lua_State *L, const std::string& path, const std::string& filename)
    {
        luaL_openlibs(L);
        int status = luaL_dofile(L, "assets/tiled/map_loader.lua");

        if (status) { throw std::runtime_error("Could not load script."); }

        return luabridge::LuaRef(luabridge::getGlobal(L, "loadMap")(std::string{ path + "/" + filename }.c_str()));
    }

    static void initTMX(TMX& tmx, luabridge::LuaRef& tmxRef)
    {
        tmx.orientation = getOrientation(tmxRef["orientation"]);
        tmx.renderorder = getRenderOrder(tmxRef["renderorder"]);
        tmx.width = tmxRef["width"];
        tmx.height = tmxRef["height"];
        tmx.tilewidth = tmxRef["tilewidth"];
        tmx.tileheight = tmxRef["tileheight"];
        tmx.nextobjectid = tmxRef["nextobjectid"];

        // tilesets initialization
        {
            luabridge::LuaRef tilesetsRef = tmxRef["tilesets"];
            for (int i = 1; !tilesetsRef[i].isNil(); ++i) {
                luabridge::LuaRef tilesetRef = tilesetsRef[i];
                luabridge::LuaRef tileoffsetRef = tilesetRef["tileoffset"];

                TMX::Tileset::TransparentColor transparentcolor{ 0, 0, 0, false };
                luabridge::LuaRef transparentcolorRef = tilesetRef["transparentcolor"];
                if (!transparentcolorRef.isNil()) {
                    std::string originalHex = transparentcolorRef;
                    std::string colorHexStr = "0x" + std::string(originalHex.begin() + 1, originalHex.end());
                    unsigned long colorHex = std::stoul(colorHexStr, nullptr, 16);
                    transparentcolor.r = (GLubyte)((0xff0000 & colorHex) >> 16);
                    transparentcolor.g = (GLubyte)((0x00ff00 & colorHex) >> 8);
                    transparentcolor.b = (GLubyte)(0x0000ff & colorHex);

                    transparentcolor.inUse = true;
                }

                TMX::Tileset tileset{
                    tilesetRef["name"],
                    tilesetRef["firstgid"],
                    tilesetRef["tilewidth"],
                    tilesetRef["tileheight"],
                    tilesetRef["spacing"],
                    tilesetRef["margin"],
                    tmx.meta.path + "/" + tilesetRef["image"].cast<std::string>(),
                    tilesetRef["imagewidth"],
                    tilesetRef["imageheight"],
                    transparentcolor,
                    {tileoffsetRef["x"], tileoffsetRef["y"]},
                    std::vector<TMX::Tileset::Terrain>(),
                    tilesetRef["tilecount"],
                    std::vector<TMX::Tileset::Tile>()
                };

                // terrains initialization
                {
                    luabridge::LuaRef terrainsRef = tilesetRef["terrains"];
                    for (int i = 1; !terrainsRef[i].isNil(); ++i) {
                        luabridge::LuaRef terrainRef = terrainsRef[i];

                        TMX::Tileset::Terrain terrain{
                            terrainRef["name"],
                            terrainRef["tile"]
                        };
                        tileset.terrains.push_back(std::move(terrain));
                    }
                } // end terrains initialization

                // tiles initialization
                {
                    luabridge::LuaRef tilesRef = tilesetRef["tiles"];
                    for (int i = 1; !tilesRef[i].isNil(); ++i) {
                        luabridge::LuaRef tileRef = tilesRef[i];

                        // objectGroup initialization
                        luabridge::LuaRef objectGroupRef = tileRef["objectGroup"];
                        TMX::Tileset::Tile::ObjectGroup objectGroup{};
                        if (objectGroupRef.isNil()) {
                            objectGroup.type = TMX::Tileset::Tile::ObjectGroup::Type::NONE;
                        }
                        else if (objectGroupRef["type"] == "objectgroup") {
                            objectGroup.type = TMX::Tileset::Tile::ObjectGroup::Type::OBJECTGROUP;
                            objectGroup.name = objectGroupRef["name"].cast<std::string>();
                            objectGroup.visible = objectGroupRef["visible"];
                            objectGroup.opacity = objectGroupRef["opacity"];
                            objectGroup.offsetx = objectGroupRef["offsetx"];
                            objectGroup.offsety = objectGroupRef["offsety"];

                            // objects initialization
                            {
                                luabridge::LuaRef objectsRef = objectGroupRef["objects"];
                                for (int i = 1; !objectsRef[i].isNil(); ++i) {
                                    luabridge::LuaRef objectRef = objectsRef[i];

                                    luabridge::LuaRef gidRef = objectRef["gid"];
                                    TMX::Tileset::Tile::ObjectGroup::Object object{
                                        objectRef["id"],
                                        objectRef["name"],
                                        objectRef["type"],
                                        getShapeEnum(objectRef["shape"]),
                                        objectRef["x"],
                                        objectRef["y"],
                                        objectRef["width"],
                                        objectRef["height"],
                                        objectRef["rotation"],
                                        gidRef.isNil() ? 0 : gidRef.cast<unsigned>(),
                                        objectRef["visible"]
                                    };

                                    objectGroup.objects.push_back(std::move(object));
                                }
                            } // end objects initialization
                        }
                        // end objectGroup initialization

                        TMX::Tileset::Tile tile{
                            tileRef["id"],
                            std::map<std::string, std::string>{},
                            std::move(objectGroup),
                            std::vector<TMX::Tileset::Tile::Frame>{},
                            std::vector<unsigned>{},
                        };

                        // properties initialization
                        {
                            luabridge::LuaRef propertiesRef = tileRef["properties"];
                            if (!propertiesRef.isNil()) {
                                for (luabridge::Iterator it(propertiesRef); !it.isNil(); ++it) {
                                    luabridge::LuaRef key = it.key();
                                    luabridge::LuaRef val = *it;
                                    tile.properties.insert(std::pair<std::string, std::string>{
                                        key,
                                        val
                                    });
                                }
                            }
                        } // end properties initialization

                        // animation initialization
                        {
                            luabridge::LuaRef animationRef = tileRef["animation"];
                            for (int i = 1; !animationRef.isNil() && !animationRef[i].isNil(); ++i) {
                                luabridge::LuaRef frameRef = animationRef[i];

                                TMX::Tileset::Tile::Frame frame{
                                    frameRef["tileid"],
                                    frameRef["duration"]
                                };

                                tile.animation.push_back(std::move(frame));
                            }
                        } // end animation initialization

                        // terrain initialization
                        {
                            luabridge::LuaRef terrainRef = tileRef["terrain"];
                            for (int i = 1; !terrainRef.isNil() && !terrainRef[i].isNil(); ++i) {
                                tile.terrain.push_back(terrainRef[i]);
                            }
                        } // end terrain initialization

                        tileset.tiles.push_back(std::move(tile));
                    }
                } // end tiles initialization

                tmx.tilesets.push_back(std::move(tileset));
            }
        } // end tilesets initialization

        // layers initialization
        {
            luabridge::LuaRef layersRef = tmxRef["layers"];
            for (int i = 1; !layersRef.isNil() && !layersRef[i].isNil(); ++i) {
                luabridge::LuaRef layerRef = layersRef[i];

                TMX::Layer::Type type = getLayerType(layerRef["type"]);

                TMX::Layer layer{
                    type,
                    layerRef["name"],
                    type == TMX::Layer::Type::OBJECTGROUP ? 0 : layerRef["x"],
                    type == TMX::Layer::Type::OBJECTGROUP ? 0 : layerRef["y"],
                    type == TMX::Layer::Type::OBJECTGROUP ? 0 : layerRef["width"],
                    type == TMX::Layer::Type::OBJECTGROUP ? 0 : layerRef["height"],
                    layerRef["visible"],
                    layerRef["opacity"],
                    layerRef["offsetx"],
                    layerRef["offsety"]
                };

                // data initialization
                {
                    luabridge::LuaRef dataRef = layerRef["data"];
                    for (int i = 1; !dataRef.isNil() && !dataRef[i].isNil(); ++i) {
                        layer.data.push_back(dataRef[i]);
                    }
                } // end data initialization

                // objects initialization
                {
                    luabridge::LuaRef objectsRef = layerRef["objects"];
                    for (int i = 1; !objectsRef.isNil() && !objectsRef[i].isNil(); ++i) {
                        luabridge::LuaRef objectRef = objectsRef[i];

                        luabridge::LuaRef gidRef = objectRef["gid"];
                        TMX::Tileset::Tile::ObjectGroup::Object object{
                            objectRef["id"],
                            objectRef["name"],
                            objectRef["type"],
                            getShapeEnum(objectRef["shape"]),
                            objectRef["x"],
                            objectRef["y"],
                            objectRef["width"],
                            objectRef["height"],
                            objectRef["rotation"],
                            gidRef.isNil() ? 0 : gidRef.cast<unsigned>(),
                            objectRef["visible"]
                        };

                        layer.objects.push_back(std::move(object));
                    }
                }

                tmx.layers.push_back(std::move(layer));
            }
        } // end layers initialization
    }

    BadFilename::BadFilename(const char* message)
        : std::runtime_error(message) {}

    TMX::Meta::Meta(const std::string& path, const std::string& file)
        : path(path), file(file) {}
    TMX::Meta::Meta(const std::string& pathfile)
        : path(), file()
    {
        // Guarantees end of string
        file = pathfile.substr(pathfile.find_last_of("\\/") + 1, pathfile.length());
        if (file.compare("") == 0) {
            throw BadFilename("TMX::Meta: Argument must contain file.");
        }
        path = pathfile.substr(0, pathfile.find_last_of("\\/"));
        if (path.compare(file) == 0) {
            path = "./";
        } else std::for_each(std::begin(path), std::end(path), [](char& ch) {
            if (ch == '\\') ch = '/';
        });
    }

    TMX::TMX(const std::string& path, const std::string& file)
        : meta(path, file)
    {
        std::unique_ptr<lua_State, std::function<void(lua_State*)>> L(
            luaL_newstate(),
            [](lua_State* L) {lua_close(L); });
        luabridge::LuaRef tmxRef = getTMXRef(L.get(), meta.path, meta.file);

        initTMX(*this, tmxRef);
    }

    TMX::TMX(const std::string& pathfile)
        : meta(pathfile)
    {
        std::unique_ptr<lua_State, std::function<void(lua_State*)>> L(
            luaL_newstate(),
            [](lua_State* L) {lua_close(L); });
        luabridge::LuaRef tmxRef = getTMXRef(L.get(), meta.path, meta.file);

        initTMX(*this, tmxRef);
    }

    unsigned getTilesetIndex(const TMX& tmx, unsigned gid)
    {
        for (auto it = tmx.tilesets.begin(); it != tmx.tilesets.end(); ++it) {
            unsigned firstInclusive = it->firstgid;
            unsigned lastExclusive = it->firstgid + it->tilecount;

            if ((gid >= firstInclusive) && (gid < lastExclusive)) {
                return it - tmx.tilesets.begin();
            }
        }
        throw std::out_of_range("No tileset for given tile ID.");
    }

    void loadObjects(
        std::shared_ptr<const TMX> pTMX,
        const glm::mat4& model,
        std::shared_ptr<MeshManager> pMeshManager,
        EntityManager& entityManager,
        TransformComponent& transformComponent,
        AnimationComponent& animationComponent,
        DataComponent* dataComponent)
    {
        AnimationFactory animationFactory(pTMX, pMeshManager);

        unsigned layerIndex = -1;
        std::for_each(std::begin(pTMX->layers), std::end(pTMX->layers), [&](const TMX::Layer& layer) {

            ++layerIndex;
            if (layer.type != TMX::Layer::Type::OBJECTGROUP) { return; }

            std::for_each(std::begin(layer.objects), std::end(layer.objects), [&](const TMX::Tileset::Tile::ObjectGroup::Object& object) {
                Entity entity = entityManager.create();

                const TMX::Tileset& tileset = pTMX->tilesets.at(getTilesetIndex(*pTMX, object.gid));
                transformComponent.setLocalTransform(
                    entity,
                    glm::scale(
                        // Subtract by height to compensate for TMX odd positions
                        glm::translate(glm::vec3((float)object.x / (float)pTMX->tilewidth, (float)(object.y - object.height) / (float)pTMX->tileheight, layerIndex)),
                        glm::vec3((float)object.width / (float)tileset.tilewidth, (float)object.height / (float)tileset.tileheight, 1.f)));

                std::shared_ptr<const Animation> pAnimation(new Animation{
                    animationFactory.create(object.gid)
                });
                animationComponent.setAnimations(entity, {
                    {0, pAnimation}
                }, 0);

                if (dataComponent) {
                    dataComponent->create(entity, object.id);
                    dataComponent->setData(entity, { "name", object.name });
                }
            });

        });
    }
}
