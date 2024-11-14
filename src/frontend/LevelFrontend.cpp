#include "LevelFrontend.hpp"

#include "ContentGfxCache.hpp"

#include "assets/Assets.hpp"
#include "audio/audio.hpp"
#include "content/Content.hpp"
#include "graphics/core/Atlas.hpp"
#include "graphics/render/BlocksPreview.hpp"
#include "logic/LevelController.hpp"
#include "logic/PlayerController.hpp"
#include "objects/Player.hpp"
#include "voxels/Block.hpp"
#include "world/Level.hpp"

LevelFrontend::LevelFrontend(
    Player* currentPlayer, LevelController* controller, Assets& assets
) : level(*controller->getLevel()),
    controller(controller),
    assets(assets),
    contentCache(std::make_unique<ContentGfxCache>(level.content, assets)) 
{
    assets.store(
        BlocksPreview::build(
            *contentCache, assets, *level.content->getIndices()
        ),
        "block-previews"
    );
    controller->getBlocksController()->listenBlockInteraction(
        [currentPlayer, controller, &assets](auto player, const auto& pos, const auto& def, BlockInteraction type) {
            const auto& level = *controller->getLevel();
            auto material = level.content->findBlockMaterial(def.material);
            if (material == nullptr) {
                return;
            }

            if (type == BlockInteraction::step) {
                auto sound = assets.get<audio::Sound>(material->stepsSound);
                glm::vec3 pos {};
                auto soundsCamera = currentPlayer->currentCamera.get();
                if (soundsCamera == currentPlayer->spCamera.get() ||
                    soundsCamera == currentPlayer->tpCamera.get()) {
                    soundsCamera = currentPlayer->fpCamera.get();
                }
                bool relative = player == currentPlayer && 
                    soundsCamera == currentPlayer->fpCamera.get();
                if (!relative) {
                    pos = player->getPosition();
                }
                audio::play(
                    sound, 
                    pos, 
                    relative, 
                    0.333f, 
                    1.0f + (rand() % 6 - 3) * 0.05f, 
                    false,
                    audio::PRIORITY_LOW,
                    audio::get_channel_index("regular")
                );
            } else {
                audio::Sound* sound = nullptr;
                switch (type) {
                    case BlockInteraction::placing:
                        sound = assets.get<audio::Sound>(material->placeSound);
                        break;
                    case BlockInteraction::destruction:
                        sound = assets.get<audio::Sound>(material->breakSound);
                        break; 
                    default:
                        break;   
                }
                audio::play(
                    sound, 
                    glm::vec3(pos.x, pos.y, pos.z) + 0.5f, 
                    false, 
                    1.0f,
                    1.0f + (rand() % 6 - 3) * 0.05f, 
                    false,
                    audio::PRIORITY_NORMAL,
                    audio::get_channel_index("regular")
                );
            }
        }
    );
}

LevelFrontend::~LevelFrontend() = default;

Level& LevelFrontend::getLevel() {
    return level;
}

const Level& LevelFrontend::getLevel() const {
    return level;
}

const Assets& LevelFrontend::getAssets() const {
    return assets;
}

const ContentGfxCache& LevelFrontend::getContentGfxCache() const {
    return *contentCache;
}

LevelController* LevelFrontend::getController() const {
    return controller;
}
