#include "scripting.hpp"

#include "scripting_commons.hpp"
#include "typedefs.hpp"
#include "lua/lua_engine.hpp"
#include "lua/lua_custom_types.hpp"
#include "content/Content.hpp"
#include "voxels/Block.hpp"
#include "voxels/Chunk.hpp"
#include "world/generator/GeneratorDef.hpp"

class LuaGeneratorScript : public GeneratorScript {
    scriptenv env;
    std::vector<Biome> biomes;
    uint biomeParameters;
    uint seaLevel;
public:
    LuaGeneratorScript(
        scriptenv env, 
        std::vector<Biome> biomes,
        uint biomeParameters,
        uint seaLevel)
    : env(std::move(env)), 
      biomes(std::move(biomes)), 
      biomeParameters(biomeParameters),
      seaLevel(seaLevel) 
      {}

    std::shared_ptr<Heightmap> generateHeightmap(
        const glm::ivec2& offset, const glm::ivec2& size, uint64_t seed
    ) override {
        auto L = lua::get_main_thread();
        lua::pushenv(L, *env);
        if (lua::getfield(L, "generate_heightmap")) {
            lua::pushivec_stack(L, offset);
            lua::pushivec_stack(L, size);
            lua::pushinteger(L, seed);
            if (lua::call_nothrow(L, 5)) {
                auto map = lua::touserdata<lua::LuaHeightmap>(L, -1)->getHeightmap();
                lua::pop(L, 2);
                return map;
            }
        }
        lua::pop(L);
        return std::make_shared<Heightmap>(size.x, size.y);
    }

    void prepare(const Content* content) override {
        for (auto& biome : biomes) {
            for (auto& layer : biome.groundLayers.layers) {
                layer.rt.id = content->blocks.require(layer.block).rt.id;
            }
            for (auto& layer : biome.seaLayers.layers) {
                layer.rt.id = content->blocks.require(layer.block).rt.id;
            }
        }
    }

    const std::vector<Biome>& getBiomes() const override {
        return biomes;
    }

    uint getBiomeParameters() const override {
        return biomeParameters;
    }

    uint getSeaLevel() const override {
        return seaLevel;
    }
};

static BlocksLayer load_layer(
    lua::State* L, int idx, uint& lastLayersHeight, bool& hasResizeableLayer
) {
    auto name = lua::require_string_field(L, "block");
    int height = lua::require_integer_field(L, "height");
    bool belowSeaLevel = lua::get_boolean_field(L, "below_sea_level", true);

    if (hasResizeableLayer) {
        lastLayersHeight += height;
    }
    if (height == -1) {
        if (hasResizeableLayer) {
            throw std::runtime_error("only one resizeable layer allowed");
        }
        hasResizeableLayer = true;
    }
    return BlocksLayer {name, height, belowSeaLevel, {}};
}

static inline BlocksLayers load_layers(
    lua::State* L, const std::string& fieldname
) {
    uint lastLayersHeight = 0;
    bool hasResizeableLayer = false;
    std::vector<BlocksLayer> layers;

    if (lua::getfield(L, fieldname)) {
        int len = lua::objlen(L, -1);
        for (int i = 1; i <= len; i++) {
            lua::rawgeti(L, i);
            try {
                layers.push_back(
                    load_layer(L, -1, lastLayersHeight, hasResizeableLayer));
            } catch (const std::runtime_error& err) {
                throw std::runtime_error(
                    fieldname+" #"+std::to_string(i)+": "+err.what());
            }
            lua::pop(L);
        }
        lua::pop(L);
    }
    return BlocksLayers {std::move(layers), lastLayersHeight};
}

static inline Biome load_biome(
    lua::State* L, const std::string& name, uint parametersCount, int idx
) {
    lua::pushvalue(L, idx);

    std::vector<BiomeParameter> parameters(parametersCount);
    lua::requirefield(L, "parameters");
    if (lua::objlen(L, -1) < parametersCount) {
        throw std::runtime_error(
            std::to_string(parametersCount)+" parameters expected");
    }
    for (uint i = 1; i <= parametersCount; i++) {
        lua::rawgeti(L, i);
        float value = lua::require_number_field(L, "value");
        float weight = lua::require_number_field(L, "weight");
        parameters.push_back(BiomeParameter {value, weight});
        lua::pop(L);
    }
    lua::pop(L);

    BlocksLayers groundLayers;
    BlocksLayers seaLayers;
    try {
        groundLayers = load_layers(L, "layers");
        seaLayers = load_layers(L, "sea_layers");
    } catch (const std::runtime_error& err) {
        throw std::runtime_error("biome "+name+": "+err.what());
    }
    lua::pop(L);
    return Biome {
        name,
        std::move(parameters),
        std::move(groundLayers),
        std::move(seaLayers)};
}

std::unique_ptr<GeneratorScript> scripting::load_generator(
    const fs::path& file
) {
    auto env = create_environment();
    auto L = lua::get_main_thread();
    lua::stackguard _(L);

    lua::pop(L, load_script(*env, "generator", file));

    lua::pushenv(L, *env);

    uint biomeParameters = lua::get_integer_field(L, "biome_parameters", 0, 0, 16);
    uint seaLevel = lua::get_integer_field(L, "sea_level", 0, 0, CHUNK_H);

    std::vector<Biome> biomes;
    lua::requirefield(L, "biomes");
    if (!lua::istable(L, -1)) {
        throw std::runtime_error("'biomes' must be a table");
    }
    lua::pushnil(L);
    while (lua::next(L, -2)) {
        lua::pushvalue(L, -2);
        std::string biomeName = lua::tostring(L, -1);
        try {
            biomes.push_back(
                load_biome(L, biomeName, biomeParameters, -2));
        } catch (const std::runtime_error& err) {
            throw std::runtime_error("biome "+biomeName+": "+err.what());
        }
        lua::pop(L, 2);
    }
    lua::pop(L);

    lua::pop(L);
    return std::make_unique<LuaGeneratorScript>(
        std::move(env), 
        std::move(biomes),
        biomeParameters,
        seaLevel);
}
