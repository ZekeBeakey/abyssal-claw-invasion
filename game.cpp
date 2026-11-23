#include "raylib.h"
#include "raymath.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

enum class EnemyType {
    SKIRMISHER, // Skitter-Claw / Acid-Spitter
    FLANKER,    // Sidestep-Crab / Venom-Flanker
    BRUISER,    // Iron-Shell / Magma-Mortar
    COMMANDER   // Claw-Lord (Wave 5, 10, 15... miniboss)
};

enum class EnemyState {
    CHASE,
    WINDUP,
    ATTACK,
    RECOVERY,
    STAGGER
};

enum class PowerUpId {
    COUNTER_SURGE,
    TEMPEST_DASH,
    HEAVY_IMPACT,
    BLADE_TEMPO,
    VITALITY_SURGE,
    SPARK_VAMPIRISM,
    COUNT
};

enum class SuperUpgradeId {
    LASER_AXE,
    INFERNO_THRUSTER,
    DEFENSE_DRONE,
    COUNT
};

struct PowerUpCard {
    PowerUpId id;
    const char* title;
    const char* statLine;
    Color accent;
};

struct SuperUpgradeCard {
    SuperUpgradeId id;
    const char* title;
    const char* statLine;
    Color accent;
};

struct BuildState {
    int stacks[(int)PowerUpId::COUNT] = {0};
    int superStacks[(int)SuperUpgradeId::COUNT] = {0};

    int Get(PowerUpId id) const {
        return stacks[(int)id];
    }

    void Add(PowerUpId id) {
        stacks[(int)id]++;
    }

    int GetSuper(SuperUpgradeId id) const {
        return superStacks[(int)id];
    }

    void AddSuper(SuperUpgradeId id) {
        superStacks[(int)id]++;
    }

    void Reset() {
        for (int i = 0; i < (int)PowerUpId::COUNT; ++i) {
            stacks[i] = 0;
        }
        for (int i = 0; i < (int)SuperUpgradeId::COUNT; ++i) {
            superStacks[i] = 0;
        }
    }
};

struct SynthAudio {
    bool ready = false;
    bool muted = false;
    Sound swing;
    Sound axeSwing;
    Sound hitLight;
    Sound hitHeavy;
    Sound counterChime;
    Sound dashWhoosh;
    Sound fireRing;
    Sound droneBolt;
    Sound healPickup;
    Sound upgradeChime;
    Sound bossAlert;
    Sound gameOverTone;

    static uint32_t NextRng(uint32_t& state) {
        state = state * 1664525u + 1013904223u;
        return state;
    }

    static Sound BuildSweepSound(float startFreq, float endFreq, float duration, float noiseMix,
                                 bool squareWave, float gain, uint32_t seed) {
        const unsigned int sampleRate = 44100;
        unsigned int sampleCount = (unsigned int)(sampleRate * duration);
        if (sampleCount < 1) sampleCount = 1;

        std::vector<int16_t> pcm(sampleCount, 0);
        uint32_t rng = seed;
        float phase = 0.0f;

        for (unsigned int i = 0; i < sampleCount; ++i) {
            float t = (float)i / (float)sampleCount;
            float env = powf(1.0f - t, 1.55f);
            float freq = startFreq + (endFreq - startFreq) * t;
            phase += freq / (float)sampleRate;
            if (phase >= 1.0f) phase -= floorf(phase);

            float tone = squareWave ? ((phase < 0.5f) ? 1.0f : -1.0f)
                                    : sinf(2.0f * PI * phase);
            float noise = ((float)(NextRng(rng) & 0xFFFFu) / 32768.0f) - 1.0f;
            float sample = ((1.0f - noiseMix) * tone + noiseMix * noise) * env * gain;
            sample = Clamp(sample, -1.0f, 1.0f);
            pcm[i] = (int16_t)(sample * 32767.0f);
        }

        Wave w = {};
        w.frameCount = sampleCount;
        w.sampleRate = sampleRate;
        w.sampleSize = 16;
        w.channels = 1;
        size_t bytes = sampleCount * sizeof(int16_t);
        w.data = MemAlloc((int)bytes);
        memcpy(w.data, pcm.data(), bytes);

        Sound s = LoadSoundFromWave(w);
        UnloadWave(w);
        return s;
    }

    static Sound BuildDualToneSound(float f1, float f2, float halfDur, bool ascending,
                                    float gain, uint32_t seed) {
        const unsigned int sampleRate = 44100;
        unsigned int halfCount = (unsigned int)(sampleRate * halfDur);
        if (halfCount < 1) halfCount = 1;
        unsigned int totalCount = halfCount * 2;

        std::vector<int16_t> pcm(totalCount, 0);
        uint32_t rng = seed;
        float phase = 0.0f;

        for (unsigned int i = 0; i < totalCount; ++i) {
            bool secondHalf = (i >= halfCount);
            float localT = secondHalf ? (float)(i - halfCount) / (float)halfCount
                                      : (float)i / (float)halfCount;
            float env = (1.0f - localT * 0.75f) * (secondHalf ? (1.0f - localT * 0.55f) : 1.0f);
            float freq = secondHalf ? f2 : f1;
            if (!ascending && secondHalf) freq = f2 * (1.0f - 0.15f * localT);

            phase += freq / (float)sampleRate;
            if (phase >= 1.0f) phase -= floorf(phase);

            float sq = (phase < 0.5f) ? 0.75f : -0.75f;
            float tri = 2.0f * fabsf(2.0f * phase - 1.0f) - 1.0f;
            float noise = (((float)(NextRng(rng) & 0xFFFFu) / 32768.0f) - 1.0f) * 0.08f;
            float sample = (0.65f * sq + 0.35f * tri + noise) * env * gain;
            sample = Clamp(sample, -1.0f, 1.0f);
            pcm[i] = (int16_t)(sample * 32767.0f);
        }

        Wave w = {};
        w.frameCount = totalCount;
        w.sampleRate = sampleRate;
        w.sampleSize = 16;
        w.channels = 1;
        size_t bytes = totalCount * sizeof(int16_t);
        w.data = MemAlloc((int)bytes);
        memcpy(w.data, pcm.data(), bytes);

        Sound s = LoadSoundFromWave(w);
        UnloadWave(w);
        return s;
    }

    void Init() {
        InitAudioDevice();
        ready = IsAudioDeviceReady();
        if (!ready) return;

        swing        = BuildSweepSound(520.0f, 130.0f, 0.11f, 0.55f, false, 0.34f, 101u);
        axeSwing     = BuildSweepSound(290.0f, 68.0f,  0.17f, 0.62f, true,  0.42f, 202u);
        hitLight     = BuildSweepSound(210.0f, 55.0f,  0.09f, 0.45f, true,  0.42f, 303u);
        hitHeavy     = BuildSweepSound(160.0f, 36.0f,  0.16f, 0.50f, true,  0.52f, 404u);
        counterChime = BuildDualToneSound(587.33f, 987.77f, 0.075f, true, 0.46f, 505u);
        dashWhoosh   = BuildSweepSound(360.0f, 110.0f, 0.14f, 0.78f, false, 0.35f, 606u);
        fireRing     = BuildSweepSound(185.0f, 58.0f,  0.24f, 0.85f, true,  0.38f, 707u);
        droneBolt    = BuildSweepSound(940.0f, 320.0f, 0.065f, 0.10f, true, 0.24f, 808u);
        healPickup   = BuildDualToneSound(523.25f, 783.99f, 0.07f, true, 0.36f, 909u);
        upgradeChime = BuildDualToneSound(440.0f, 880.0f, 0.09f, true, 0.42f, 1010u);
        bossAlert    = BuildDualToneSound(146.83f, 98.0f, 0.18f, false, 0.50f, 1111u);
        gameOverTone = BuildDualToneSound(293.66f, 130.81f, 0.22f, false, 0.48f, 1212u);
    }

    void Shutdown() {
        if (!ready) return;
        UnloadSound(swing);
        UnloadSound(axeSwing);
        UnloadSound(hitLight);
        UnloadSound(hitHeavy);
        UnloadSound(counterChime);
        UnloadSound(dashWhoosh);
        UnloadSound(fireRing);
        UnloadSound(droneBolt);
        UnloadSound(healPickup);
        UnloadSound(upgradeChime);
        UnloadSound(bossAlert);
        UnloadSound(gameOverTone);
        CloseAudioDevice();
        ready = false;
    }

    void Play(Sound s, float pitch = 1.0f) const {
        if (!ready || muted) return;
        SetSoundPitch(s, pitch);
        PlaySound(s);
    }
};

struct Enemy {
    const char* name = "Skitter-Claw";
    Vector2 pos;
    Vector2 vel = {0.0f, 0.0f};
    float radius = 18.0f;
    int hp = 50;
    int maxHp = 50;
    float speed = 120.0f;

    EnemyType type = EnemyType::SKIRMISHER;
    bool isRanged = false;
    float flankSign = 1.0f;
    float triggerRange = 68.0f;
    float windupDuration = 0.54f;
    float attackDuration = 0.18f;
    float lungeSpeed = 305.0f;
    float recoveryDuration = 0.62f;
    float staggerResist = 0.75f;
    float knockbackResist = 0.90f;

    EnemyState state = EnemyState::CHASE;
    float stateTimer = 0.0f;
    float attackCooldown = 0.0f;
    float hitFlashTimer = 0.0f;
    Vector2 attackDir = {1.0f, 0.0f};
    bool hasDealtDamage = false;
    bool hitThisSwing = false;
};

struct EnemyProjectile {
    Vector2 pos;
    Vector2 vel;
    float radius = 6.5f;
    int damage = 12;
    Color color = ORANGE;
    float life = 3.2f;
};

struct FireRingZone {
    Vector2 pos;
    float radius = 78.0f;
    int tickDamage = 9;
    float tickTimer = 0.0f;
    float tickInterval = 0.28f;
    float timer = 0.0f;
    float maxTimer = 1.85f;
};

struct DroneBolt {
    Vector2 pos;
    Vector2 vel;
    float radius = 5.0f;
    int damage = 18;
    float life = 1.2f;
};

struct DamagePopup {
    Vector2 pos;
    int amount;
    float timer;
    float maxTimer;
    bool isFinisher;
    bool isCounter;
    bool isHeal = false;
};

struct DashAfterimage {
    Vector2 pos;
    Vector2 aimDir;
    float radius;
    float timer;
    float maxTimer;
};

struct HitSpark {
    Vector2 pos;
    Vector2 vel;
    Color color;
    float size;
    float timer;
    float maxTimer;
};

struct HealthPickup {
    Vector2 pos;
    float radius = 10.0f;
    int healAmount = 18;
    float timer = 0.0f;
    float maxTimer = 10.0f;
};

static const char* ResolveEnemyName(EnemyType type, bool isRanged) {
    if (type == EnemyType::COMMANDER) return "Claw-Lord";
    if (isRanged) {
        switch (type) {
            case EnemyType::SKIRMISHER: return "Acid-Spitter";
            case EnemyType::FLANKER:    return "Venom-Flanker";
            case EnemyType::BRUISER:    return "Magma-Mortar";
            default:                    return "Ranged Claw";
        }
    }
    switch (type) {
        case EnemyType::SKIRMISHER: return "Skitter-Claw";
        case EnemyType::FLANKER:    return "Sidestep-Crab";
        case EnemyType::BRUISER:    return "Iron-Shell";
        default:                    return "Claw-Spawn";
    }
}

static const char* EnemyArchetypeName(const Enemy& e) {
    return e.name;
}

static PowerUpCard MakePowerUpCard(PowerUpId id) {
    switch (id) {
        case PowerUpId::COUNTER_SURGE:
            return {id, "Counter Surge", "+22% Counter Dmg & +4 HP on Counter", LIME};
        case PowerUpId::TEMPEST_DASH:
            return {id, "Thruster Overdrive", "-14% Dash CD & +12 Dash-Strike Dmg", SKYBLUE};
        case PowerUpId::HEAVY_IMPACT:
            return {id, "Shell-Breaker Core", "+25% Finisher Dmg & Extra Knockback", GOLD};
        case PowerUpId::BLADE_TEMPO:
            return {id, "Arc-Blade Tempo", "+12% Move Speed & +6 Base Dmg", ORANGE};
        case PowerUpId::VITALITY_SURGE:
            return {id, "Aegis Hull Plating", "+25 Max HP, Full Heal & +6% Core Drop", GREEN};
        case PowerUpId::SPARK_VAMPIRISM:
        default:
            return {PowerUpId::SPARK_VAMPIRISM, "Bio-Siphon Matrix", "+1 Heal on Counter/Finisher & +10% Score", VIOLET};
    }
}

static SuperUpgradeCard MakeSuperUpgradeCard(SuperUpgradeId id) {
    switch (id) {
        case SuperUpgradeId::LASER_AXE:
            return {id, "Laser Axe Overhaul", "Wider Cleave Arc, +Reach & +14 Swing Dmg", GOLD};
        case SuperUpgradeId::INFERNO_THRUSTER:
            return {id, "Inferno Ring Thrusters", "Dashing Leaves a Burning Ring of Fire", ORANGE};
        case SuperUpgradeId::DEFENSE_DRONE:
        default:
            return {SuperUpgradeId::DEFENSE_DRONE, "Aegis Striker Drone", "Flying Drone Auto-Fires at Nearest Crab", SKYBLUE};
    }
}

static void RollDraftChoices(PowerUpCard choices[3]) {
    std::vector<PowerUpId> pool;
    for (int i = 0; i < (int)PowerUpId::COUNT; ++i) {
        pool.push_back((PowerUpId)i);
    }

    for (int slot = 0; slot < 3 && !pool.empty(); ++slot) {
        int idx = GetRandomValue(0, (int)pool.size() - 1);
        choices[slot] = MakePowerUpCard(pool[idx]);
        pool.erase(pool.begin() + idx);
    }
}

static void RollSuperChoices(SuperUpgradeCard choices[3]) {
    for (int slot = 0; slot < 3; ++slot) {
        choices[slot] = MakeSuperUpgradeCard((SuperUpgradeId)slot);
    }
}

static std::string FormatBuildSummary(const BuildState& build) {
    std::string summary;

    for (int i = 0; i < (int)SuperUpgradeId::COUNT; ++i) {
        int st = build.superStacks[i];
        if (st > 0) {
            SuperUpgradeCard sc = MakeSuperUpgradeCard((SuperUpgradeId)i);
            if (!summary.empty()) summary += "  |  ";
            summary += "[SUPER] ";
            summary += sc.title;
            if (st > 1) {
                summary += " x" + std::to_string(st);
            }
        }
    }

    for (int i = 0; i < (int)PowerUpId::COUNT; ++i) {
        int st = build.stacks[i];
        if (st > 0) {
            PowerUpCard card = MakePowerUpCard((PowerUpId)i);
            if (!summary.empty()) summary += "  |  ";
            summary += card.title;
            if (st > 1) {
                summary += " x" + std::to_string(st);
            }
        }
    }

    if (summary.empty()) {
        return "Standard Exo-Loadout (No mods drafted yet)";
    }
    return summary;
}

static const char* EnemyStateLabel(EnemyState s) {
    switch (s) {
        case EnemyState::CHASE:    return "CHASE";
        case EnemyState::WINDUP:   return "WINDUP";
        case EnemyState::ATTACK:   return "ATTACK";
        case EnemyState::RECOVERY: return "REC";
        case EnemyState::STAGGER:  return "STAGGER";
        default:                   return "?";
    }
}

static Vector2 ClampToScreen(Vector2 pos, float radius, int screenW, int screenH) {
    pos.x = Clamp(pos.x, radius, (float)screenW - radius);
    pos.y = Clamp(pos.y, radius, (float)screenH - radius);
    return pos;
}

static Vector2 PickPerimeterSpawn(int index, int screenW, int screenH) {
    int side = index % 4;
    float margin = 48.0f;
    if (side == 0) return {margin, GetRandomValue((int)margin, screenH - (int)margin) * 1.0f};
    if (side == 1) return {screenW - margin, GetRandomValue((int)margin, screenH - (int)margin) * 1.0f};
    if (side == 2) return {GetRandomValue((int)margin, screenW - (int)margin) * 1.0f, margin};
    return {GetRandomValue((int)margin, screenW - (int)margin) * 1.0f, screenH - margin};
}

static Enemy MakeEnemy(EnemyType type, Vector2 spawnPos, int wave, bool rangedVariant) {
    Enemy e;
    e.type = type;
    e.isRanged = (type != EnemyType::COMMANDER) && rangedVariant;
    e.name = ResolveEnemyName(type, e.isRanged);
    e.pos = spawnPos;
    e.flankSign = (GetRandomValue(0, 1) == 0) ? -1.0f : 1.0f;
    e.attackCooldown = (float)GetRandomValue(35, 105) / 100.0f;

    float waveHpScale = 1.0f + (wave - 1) * 0.16f;
    float waveSpeedScale = 1.0f + fminf((wave - 1) * 0.015f, 0.10f);

    if (type == EnemyType::SKIRMISHER) {
        e.radius = 16.0f;
        e.maxHp = (int)((e.isRanged ? 34.0f : 38.0f) * waveHpScale);
        e.speed = (e.isRanged ? 98.0f : 114.0f) * waveSpeedScale;
        e.triggerRange = e.isRanged ? 310.0f : 62.0f;
        e.windupDuration = e.isRanged ? 0.62f : 0.56f;
        e.attackDuration = 0.18f;
        e.lungeSpeed = 300.0f;
        e.recoveryDuration = e.isRanged ? 0.85f : 0.60f;
        e.staggerResist = 0.75f;
        e.knockbackResist = 0.90f;
    } else if (type == EnemyType::FLANKER) {
        e.radius = 17.0f;
        e.maxHp = (int)((e.isRanged ? 44.0f : 50.0f) * waveHpScale);
        e.speed = (e.isRanged ? 96.0f : 108.0f) * waveSpeedScale;
        e.triggerRange = e.isRanged ? 340.0f : 70.0f;
        e.windupDuration = e.isRanged ? 0.68f : 0.64f;
        e.attackDuration = 0.20f;
        e.lungeSpeed = 290.0f;
        e.recoveryDuration = e.isRanged ? 0.92f : 0.64f;
        e.staggerResist = 0.70f;
        e.knockbackResist = 0.85f;
    } else if (type == EnemyType::BRUISER) {
        e.radius = 25.0f;
        e.maxHp = (int)((e.isRanged ? 92.0f : 100.0f) * waveHpScale);
        e.speed = (e.isRanged ? 52.0f : 60.0f) * waveSpeedScale;
        e.triggerRange = e.isRanged ? 360.0f : 82.0f;
        e.windupDuration = e.isRanged ? 0.88f : 0.95f;
        e.attackDuration = 0.24f;
        e.lungeSpeed = 235.0f;
        e.recoveryDuration = e.isRanged ? 1.10f : 0.76f;
        e.staggerResist = 0.35f;
        e.knockbackResist = 0.40f;
    } else {
        float bossScale = 1.0f + ((wave / 5) - 1) * 0.35f;
        if (bossScale < 1.0f) bossScale = 1.0f;
        e.radius = 34.0f;
        e.maxHp = (int)(260.0f * bossScale);
        e.speed = 76.0f * waveSpeedScale;
        e.triggerRange = 104.0f;
        e.windupDuration = 1.05f;
        e.attackDuration = 0.26f;
        e.lungeSpeed = 325.0f;
        e.recoveryDuration = 0.72f;
        e.staggerResist = 0.18f;
        e.knockbackResist = 0.15f;
        e.attackCooldown = 0.45f;
    }

    e.hp = e.maxHp;
    return e;
}

static void SpawnWave(std::vector<Enemy>& enemies, int wave, int screenW, int screenH, SynthAudio& audio) {
    enemies.clear();
    bool isBossWave = (wave > 0 && wave % 5 == 0);
    int rangedChance = (wave <= 1) ? 18 : ((wave < 5) ? (22 + wave * 6) : (34 + (wave - 5) * 4));
    if (rangedChance > 58) rangedChance = 58;

    if (isBossWave) {
        audio.Play(audio.bossAlert, 0.95f);
        Vector2 bossPos = {screenW * 0.5f, 54.0f};
        enemies.push_back(MakeEnemy(EnemyType::COMMANDER, bossPos, wave, false));

        int escortCount = 2 + (wave / 5);
        if (escortCount > 5) escortCount = 5;
        for (int i = 0; i < escortCount; ++i) {
            Vector2 pos = PickPerimeterSpawn(i + 1, screenW, screenH);
            EnemyType escortType = (i == 0) ? EnemyType::BRUISER
                                 : ((i % 2 == 1) ? EnemyType::FLANKER : EnemyType::SKIRMISHER);
            bool makeRanged = (GetRandomValue(0, 99) < rangedChance);
            enemies.push_back(MakeEnemy(escortType, pos, wave, makeRanged));
        }
        return;
    }

    int count = 2 + wave;
    if (count > 8) count = 8;

    for (int i = 0; i < count; ++i) {
        Vector2 pos = PickPerimeterSpawn(i, screenW, screenH);

        EnemyType chosenType = EnemyType::SKIRMISHER;
        int roll = GetRandomValue(0, 99);
        if (wave == 1) {
            chosenType = (i == count - 1 && count >= 3) ? EnemyType::BRUISER
                       : (roll < 55 ? EnemyType::SKIRMISHER : EnemyType::FLANKER);
        } else if (wave == 2) {
            chosenType = (roll < 42) ? EnemyType::SKIRMISHER
                       : (roll < 76 ? EnemyType::FLANKER : EnemyType::BRUISER);
        } else {
            chosenType = (roll < 36) ? EnemyType::SKIRMISHER
                       : (roll < 70 ? EnemyType::FLANKER : EnemyType::BRUISER);
        }

        bool makeRanged = (GetRandomValue(0, 99) < rangedChance);
        enemies.push_back(MakeEnemy(chosenType, pos, wave, makeRanged));
    }
}

static void ApplyHitToEnemy(Enemy& e, int damage, Vector2 hitDir, float baseKnockback, float baseStagger, bool brokeWindup) {
    e.hp -= damage;
    e.hitFlashTimer = 0.075f;
    float finalKnockback = baseKnockback * e.knockbackResist;
    float finalStagger = baseStagger * e.staggerResist;

    if ((e.type == EnemyType::BRUISER || e.type == EnemyType::COMMANDER) && finalStagger < 0.14f && !brokeWindup) {
        e.vel = Vector2Add(e.vel, Vector2Scale(hitDir, finalKnockback * 0.5f));
        return;
    }

    e.vel = Vector2Scale(hitDir, finalKnockback);
    e.state = EnemyState::STAGGER;
    e.stateTimer = finalStagger;
    e.attackCooldown = brokeWindup ? 0.55f : 0.35f;
    e.hasDealtDamage = false;
}

static void SpawnGhost(std::vector<DashAfterimage>& ghosts, Vector2 pos, Vector2 aimDir, float radius) {
    DashAfterimage g;
    g.pos = pos;
    g.aimDir = aimDir;
    g.radius = radius;
    g.timer = 0.0f;
    g.maxTimer = 0.20f;
    ghosts.push_back(g);
}

static void SpawnHitSparks(std::vector<HitSpark>& sparks, Vector2 origin, Vector2 dir, int comboStep, bool brokeWindup) {
    float baseAngle = atan2f(dir.y, dir.x);
    int count = (comboStep == 3) ? 16 : 8;
    if (brokeWindup) count += 12;

    for (int i = 0; i < count; ++i) {
        float spread = brokeWindup ? 1.55f : ((comboStep == 3) ? 1.15f : 0.85f);
        float offsetAngle = ((float)GetRandomValue(-100, 100) / 100.0f) * spread;
        float angle = baseAngle + offsetAngle;
        float speed = (float)GetRandomValue(150, (comboStep == 3 || brokeWindup) ? 460 : 290);

        HitSpark s;
        s.pos = origin;
        s.vel = {cosf(angle) * speed, sinf(angle) * speed};
        s.timer = 0.0f;
        s.maxTimer = (float)GetRandomValue(14, (comboStep == 3 || brokeWindup) ? 32 : 22) / 100.0f;
        s.size = (float)GetRandomValue(3, (comboStep == 3 || brokeWindup) ? 7 : 5);

        if (brokeWindup) {
            int cRoll = i % 3;
            s.color = (cRoll == 0) ? GOLD : ((cRoll == 1) ? LIME : RAYWHITE);
        } else if (comboStep == 3) {
            s.color = (GetRandomValue(0, 1) == 0) ? GOLD : ORANGE;
        } else {
            s.color = (GetRandomValue(0, 2) == 0) ? SKYBLUE : RAYWHITE;
        }

        sparks.push_back(s);
    }
}

static void DrawCrabSilhouette(Vector2 pos, float radius, const Enemy& e, Color bodyColor, Color trimColor, Vector2 aimDir) {
    float angle = atan2f(aimDir.y, aimDir.x);
    float cs = cosf(angle);
    float sn = sinf(angle);

    auto toWorld = [&](float lx, float ly) -> Vector2 {
        return {pos.x + lx * cs - ly * sn, pos.y + lx * sn + ly * cs};
    };

    if (e.type == EnemyType::COMMANDER) {
        DrawCircleLines((int)pos.x, (int)pos.y, radius + 5.0f, Fade(GOLD, 0.55f));
    } else if (e.isRanged) {
        DrawCircleLines((int)pos.x, (int)pos.y, radius + 3.0f, Fade(VIOLET, 0.65f));
    }

    DrawCircleV(pos, radius, bodyColor);
    DrawCircleLines((int)pos.x, (int)pos.y, radius, trimColor);

    float clawReach = radius * ((e.type == EnemyType::COMMANDER) ? 1.28f : 1.15f);
    float clawSpread = radius * 0.94f;
    Vector2 leftClaw = toWorld(clawReach * 0.68f, -clawSpread);
    Vector2 rightClaw = toWorld(clawReach * 0.68f, clawSpread);
    Vector2 leftBase = toWorld(radius * 0.35f, -radius * 0.65f);
    Vector2 rightBase = toWorld(radius * 0.35f, radius * 0.65f);

    float armThick = (e.type == EnemyType::COMMANDER) ? 6.5f : ((e.type == EnemyType::BRUISER) ? 5.0f : 3.5f);
    DrawLineEx(leftBase, leftClaw, armThick, trimColor);
    DrawLineEx(rightBase, rightClaw, armThick, trimColor);
    DrawCircleV(leftClaw, radius * 0.36f, bodyColor);
    DrawCircleV(rightClaw, radius * 0.36f, bodyColor);
    DrawCircleLines((int)leftClaw.x, (int)leftClaw.y, radius * 0.36f, trimColor);
    DrawCircleLines((int)rightClaw.x, (int)rightClaw.y, radius * 0.36f, trimColor);

    if (e.type == EnemyType::COMMANDER) {
        Vector2 hornTip = toWorld(radius * 1.15f, 0.0f);
        Vector2 hornL = toWorld(radius * 0.62f, -radius * 0.36f);
        Vector2 hornR = toWorld(radius * 0.62f, radius * 0.36f);
        DrawLineEx(hornL, hornTip, 3.5f, GOLD);
        DrawLineEx(hornR, hornTip, 3.5f, GOLD);
    } else if (e.isRanged) {
        Vector2 barrelTip = toWorld(radius * 1.22f, 0.0f);
        Vector2 barrelBase = toWorld(radius * 0.45f, 0.0f);
        DrawLineEx(barrelBase, barrelTip, 3.5f, VIOLET);
        DrawCircleV(barrelTip, 3.5f, MAGENTA);
    }

    Vector2 eyeL = toWorld(radius * 0.55f, -radius * 0.28f);
    Vector2 eyeR = toWorld(radius * 0.55f, radius * 0.28f);
    float eyeRSize = (e.type == EnemyType::COMMANDER) ? 3.5f : 2.5f;
    DrawCircleV(eyeL, eyeRSize, e.isRanged ? MAGENTA : YELLOW);
    DrawCircleV(eyeR, eyeRSize, e.isRanged ? MAGENTA : YELLOW);

    if (e.type == EnemyType::BRUISER || e.type == EnemyType::COMMANDER) {
        DrawCircleLines((int)pos.x, (int)pos.y, radius - 4.5f, GOLD);
    }
}

static void DrawExoSuitDefender(Vector2 pos, float radius, Vector2 aimDir,
                                Color armorColor, Color pauldronColor, Color visorColor,
                                bool isDashing, float animTime) {
    float angle = atan2f(aimDir.y, aimDir.x);
    float cs = cosf(angle);
    float sn = sinf(angle);

    auto toWorld = [&](float lx, float ly) -> Vector2 {
        return {pos.x + lx * cs - ly * sn, pos.y + lx * sn + ly * cs};
    };

    float jetScale = isDashing ? 1.55f : (0.75f + 0.20f * sinf(animTime * 24.0f));
    Vector2 jetLBase = toWorld(-radius * 0.85f, -radius * 0.42f);
    Vector2 jetLTip  = toWorld(-radius * (1.20f * jetScale), -radius * 0.42f);
    Vector2 jetRBase = toWorld(-radius * 0.85f, radius * 0.42f);
    Vector2 jetRTip  = toWorld(-radius * (1.20f * jetScale), radius * 0.42f);
    DrawLineEx(jetLBase, jetLTip, isDashing ? 4.5f : 3.0f, ORANGE);
    DrawLineEx(jetRBase, jetRTip, isDashing ? 4.5f : 3.0f, ORANGE);
    DrawLineEx(jetLBase, toWorld(-radius * (1.02f * jetScale), -radius * 0.42f), 2.0f, YELLOW);
    DrawLineEx(jetRBase, toWorld(-radius * (1.02f * jetScale), radius * 0.42f), 2.0f, YELLOW);

    Vector2 packL = toWorld(-radius * 0.62f, -radius * 0.44f);
    Vector2 packR = toWorld(-radius * 0.62f, radius * 0.44f);
    DrawCircleV(packL, radius * 0.24f, Color{42, 52, 74, 255});
    DrawCircleV(packR, radius * 0.24f, Color{42, 52, 74, 255});
    DrawCircleLines((int)packL.x, (int)packL.y, radius * 0.24f, pauldronColor);
    DrawCircleLines((int)packR.x, (int)packR.y, radius * 0.24f, pauldronColor);

    Vector2 chestFront = toWorld(radius * 0.56f, 0.0f);
    Vector2 chestL     = toWorld(-radius * 0.36f, -radius * 0.68f);
    Vector2 chestR     = toWorld(-radius * 0.36f, radius * 0.68f);
    DrawTriangle(chestFront, chestL, chestR, armorColor);
    DrawTriangle(chestFront, chestR, chestL, armorColor);
    DrawLineEx(chestFront, chestL, 2.2f, pauldronColor);
    DrawLineEx(chestFront, chestR, 2.2f, pauldronColor);
    DrawLineEx(chestL, chestR, 2.0f, pauldronColor);

    Vector2 shLFront = toWorld(radius * 0.32f, -radius * 0.62f);
    Vector2 shLOuter = toWorld(-radius * 0.16f, -radius * 1.05f);
    Vector2 shLRear  = toWorld(-radius * 0.56f, -radius * 0.84f);
    DrawTriangle(shLFront, shLOuter, shLRear, pauldronColor);
    DrawTriangle(shLFront, shLRear, shLOuter, pauldronColor);
    DrawLineEx(shLFront, shLOuter, 2.0f, RAYWHITE);
    DrawLineEx(shLOuter, shLRear, 2.0f, RAYWHITE);

    Vector2 shRFront = toWorld(radius * 0.32f, radius * 0.62f);
    Vector2 shROuter = toWorld(-radius * 0.16f, radius * 1.05f);
    Vector2 shRRear  = toWorld(-radius * 0.56f, radius * 0.84f);
    DrawTriangle(shRFront, shRRear, shROuter, pauldronColor);
    DrawTriangle(shRFront, shROuter, shRRear, pauldronColor);
    DrawLineEx(shRFront, shROuter, 2.0f, RAYWHITE);
    DrawLineEx(shROuter, shRRear, 2.0f, RAYWHITE);

    Vector2 armBase = toWorld(radius * 0.24f, radius * 0.78f);
    Vector2 armTip  = toWorld(radius * 1.18f, radius * 0.56f);
    DrawLineEx(armBase, armTip, 4.0f, pauldronColor);
    DrawLineEx(armBase, armTip, 1.8f, RAYWHITE);

    Vector2 helmCenter = toWorld(radius * 0.28f, 0.0f);
    DrawCircleV(helmCenter, radius * 0.42f, armorColor);
    DrawCircleLines((int)helmCenter.x, (int)helmCenter.y, radius * 0.42f, RAYWHITE);

    Vector2 visorTip = toWorld(radius * 0.68f, 0.0f);
    Vector2 visorL   = toWorld(radius * 0.34f, -radius * 0.26f);
    Vector2 visorR   = toWorld(radius * 0.34f, radius * 0.26f);
    DrawTriangle(visorTip, visorL, visorR, visorColor);
    DrawTriangle(visorTip, visorR, visorL, visorColor);
}

static void DrawSweepingSlashArc(Vector2 origin, Vector2 aimDir, float innerR, float outerR,
                                  float halfArcRad, float sweepProgress, bool flipSweep,
                                  Color fillColor, Color edgeColor) {
    float centerAngle = atan2f(aimDir.y, aimDir.x);
    float dirSign = flipSweep ? -1.0f : 1.0f;

    float activeSweep = Clamp(0.35f + 0.65f * sweepProgress, 0.25f, 1.0f);
    float startA = centerAngle - halfArcRad * dirSign;
    float endA = centerAngle + halfArcRad * dirSign * activeSweep;
    if (startA > endA) {
        float tmp = startA;
        startA = endA;
        endA = tmp;
    }

    const int segments = 22;
    std::vector<Vector2> outerPts;
    std::vector<Vector2> innerPts;
    outerPts.reserve(segments + 1);
    innerPts.reserve(segments + 1);

    for (int i = 0; i <= segments; ++i) {
        float t = (float)i / (float)segments;
        float a = startA + (endA - startA) * t;
        float tipTaper = 1.0f - 0.32f * powf(fabsf(t - 0.5f) * 2.0f, 1.6f);
        float rOut = innerR + (outerR - innerR) * tipTaper;
        outerPts.push_back({origin.x + cosf(a) * rOut, origin.y + sinf(a) * rOut});
        innerPts.push_back({origin.x + cosf(a) * innerR, origin.y + sinf(a) * innerR});
    }

    for (int i = 0; i < segments; ++i) {
        DrawTriangle(innerPts[i], outerPts[i], outerPts[i + 1], fillColor);
        DrawTriangle(innerPts[i], outerPts[i + 1], innerPts[i + 1], fillColor);
    }

    for (int i = 0; i < segments; ++i) {
        DrawLineEx(outerPts[i], outerPts[i + 1], 3.2f, edgeColor);
    }
}

int main() {
    const int screenW = 1024;
    const int screenH = 640;
    InitWindow(screenW, screenH, "The Abyssal Claw Invasion!");
    SetTargetFPS(60);

    SynthAudio audio;
    audio.Init();

    Vector2 playerPos = {screenW * 0.5f, screenH * 0.5f};
    float playerRadius = 18.0f;
    const float baseMoveSpeed = 250.0f;
    int playerHp = 100;
    int playerMaxHp = 100;
    Vector2 aimDir = {1.0f, 0.0f};

    bool isDashing = false;
    float dashTimer = 0.0f;
    float dashCooldown = 0.0f;
    float dashStrikeFollowupTimer = 0.0f;
    float afterimageSpawnTimer = 0.0f;
    const float afterimageSpawnInterval = 0.025f;
    Vector2 dashDir = {1.0f, 0.0f};
    const float dashDuration = 0.16f;
    const float dashSpeed = 680.0f;
    const float baseDashCooldownMax = 0.65f;

    bool isDashStrike = false;
    int comboStep = 0;
    float attackTimer = 0.0f;
    float attackMaxTimer = 0.15f;
    float comboResetTimer = 0.0f;
    float inputBufferTimer = 0.0f;
    const float inputBufferWindow = 0.22f;

    BuildState build;
    bool choosingUpgrade = false;
    bool choosingSuperUpgrade = false;
    PowerUpCard draftChoices[3];
    SuperUpgradeCard superChoices[3];
    int countersLanded = 0;
    int bestWave = 1;
    int bestScore = 0;
    bool playedDeathSound = false;

    float droneOrbitAngle = 0.0f;
    float droneFireTimer = 0.0f;

    int wave = 1;
    int score = 0;
    bool debugOverlay = false;
    float hitStopTimer = 0.0f;
    float screenShakeTimer = 0.0f;
    float screenShakeMag = 0.0f;
    std::vector<Enemy> enemies;
    std::vector<EnemyProjectile> enemyProjectiles;
    std::vector<FireRingZone> fireRings;
    std::vector<DroneBolt> droneBolts;
    std::vector<DamagePopup> popups;
    std::vector<DashAfterimage> afterimages;
    std::vector<HitSpark> sparks;
    std::vector<HealthPickup> pickups;

    SpawnWave(enemies, wave, screenW, screenH, audio);

    auto advanceToNextWave = [&]() {
        choosingUpgrade = false;
        choosingSuperUpgrade = false;
        wave++;
        enemyProjectiles.clear();
        fireRings.clear();
        droneBolts.clear();
        SpawnWave(enemies, wave, screenW, screenH, audio);
    };

    auto applyUpgrade = [&](PowerUpId id) {
        audio.Play(audio.upgradeChime, 1.0f);
        build.Add(id);
        if (id == PowerUpId::VITALITY_SURGE) {
            playerMaxHp += 25;
            playerHp = playerMaxHp;
        } else {
            playerHp = (int)fminf((float)playerMaxHp, (float)(playerHp + 15));
        }

        DamagePopup boonPop;
        boonPop.pos = {playerPos.x, playerPos.y - playerRadius - 18.0f};
        boonPop.amount = 15;
        boonPop.timer = 0.0f;
        boonPop.maxTimer = 0.85f;
        boonPop.isFinisher = true;
        boonPop.isCounter = false;
        boonPop.isHeal = true;
        popups.push_back(boonPop);

        advanceToNextWave();
    };

    auto applySuperUpgrade = [&](SuperUpgradeId id) {
        audio.Play(audio.upgradeChime, 1.18f);
        build.AddSuper(id);
        playerHp = (int)fminf((float)playerMaxHp, (float)(playerHp + 25));

        DamagePopup superPop;
        superPop.pos = {playerPos.x, playerPos.y - playerRadius - 22.0f};
        superPop.amount = 25;
        superPop.timer = 0.0f;
        superPop.maxTimer = 0.95f;
        superPop.isFinisher = true;
        superPop.isCounter = true;
        superPop.isHeal = true;
        popups.push_back(superPop);

        advanceToNextWave();
    };

    while (!WindowShouldClose()) {
        float rawDt = GetFrameTime();
        if (rawDt > 0.05f) rawDt = 0.05f;

        if (IsKeyPressed(KEY_F1)) {
            debugOverlay = !debugOverlay;
        }
        if (IsKeyPressed(KEY_M)) {
            audio.muted = !audio.muted;
        }

        if (wave > bestWave) bestWave = wave;
        if (score > bestScore) bestScore = score;

        if (screenShakeTimer > 0.0f) {
            screenShakeTimer -= rawDt;
            if (screenShakeTimer <= 0.0f) screenShakeMag = 0.0f;
        }

        for (size_t i = 0; i < popups.size(); ) {
            popups[i].timer += rawDt;
            popups[i].pos.y -= 42.0f * rawDt;
            if (popups[i].timer >= popups[i].maxTimer) {
                popups.erase(popups.begin() + i);
            } else {
                ++i;
            }
        }

        for (size_t i = 0; i < afterimages.size(); ) {
            afterimages[i].timer += rawDt;
            if (afterimages[i].timer >= afterimages[i].maxTimer) {
                afterimages.erase(afterimages.begin() + i);
            } else {
                ++i;
            }
        }

        for (size_t i = 0; i < sparks.size(); ) {
            sparks[i].timer += rawDt;
            sparks[i].pos = Vector2Add(sparks[i].pos, Vector2Scale(sparks[i].vel, rawDt));
            sparks[i].vel = Vector2Scale(sparks[i].vel, powf(0.015f, rawDt));
            if (sparks[i].timer >= sparks[i].maxTimer) {
                sparks.erase(sparks.begin() + i);
            } else {
                ++i;
            }
        }

        int activeAttackers = 0;
        for (const Enemy& other : enemies) {
            if (other.hp > 0 && (other.state == EnemyState::WINDUP || other.state == EnemyState::ATTACK)) {
                activeAttackers++;
            }
        }
        const int maxSimultaneousAttackers = (wave >= 4) ? 3 : 2;

        if (hitStopTimer > 0.0f) {
            hitStopTimer -= rawDt;
        } else if (choosingSuperUpgrade) {
            int pickedSlot = -1;
            if (IsKeyPressed(KEY_ONE) || IsKeyPressed(KEY_KP_1)) pickedSlot = 0;
            else if (IsKeyPressed(KEY_TWO) || IsKeyPressed(KEY_KP_2)) pickedSlot = 1;
            else if (IsKeyPressed(KEY_THREE) || IsKeyPressed(KEY_KP_3)) pickedSlot = 2;

            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                Vector2 m = GetMousePosition();
                float cardW = 290.0f;
                float cardH = 132.0f;
                float gap = 20.0f;
                float totalW = cardW * 3.0f + gap * 2.0f;
                float startX = (screenW - totalW) * 0.5f;
                float cardY = screenH * 0.5f - cardH * 0.5f + 16.0f;

                for (int s = 0; s < 3; ++s) {
                    Rectangle r = {startX + s * (cardW + gap), cardY, cardW, cardH};
                    if (CheckCollisionPointRec(m, r)) {
                        pickedSlot = s;
                        break;
                    }
                }
            }

            if (pickedSlot >= 0 && pickedSlot < 3) {
                applySuperUpgrade(superChoices[pickedSlot].id);
            }
        } else if (choosingUpgrade) {
            int pickedSlot = -1;
            if (IsKeyPressed(KEY_ONE) || IsKeyPressed(KEY_KP_1)) pickedSlot = 0;
            else if (IsKeyPressed(KEY_TWO) || IsKeyPressed(KEY_KP_2)) pickedSlot = 1;
            else if (IsKeyPressed(KEY_THREE) || IsKeyPressed(KEY_KP_3)) pickedSlot = 2;

            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                Vector2 m = GetMousePosition();
                float cardW = 290.0f;
                float cardH = 126.0f;
                float gap = 20.0f;
                float totalW = cardW * 3.0f + gap * 2.0f;
                float startX = (screenW - totalW) * 0.5f;
                float cardY = screenH * 0.5f - cardH * 0.5f + 16.0f;

                for (int s = 0; s < 3; ++s) {
                    Rectangle r = {startX + s * (cardW + gap), cardY, cardW, cardH};
                    if (CheckCollisionPointRec(m, r)) {
                        pickedSlot = s;
                        break;
                    }
                }
            }

            if (pickedSlot >= 0 && pickedSlot < 3) {
                applyUpgrade(draftChoices[pickedSlot].id);
            }
        } else {
            float dt = rawDt;

            float moveSpeed = baseMoveSpeed * (1.0f + 0.12f * build.Get(PowerUpId::BLADE_TEMPO));
            float dashCooldownMax = baseDashCooldownMax * powf(0.86f, (float)build.Get(PowerUpId::TEMPEST_DASH));
            if (dashCooldownMax < 0.24f) dashCooldownMax = 0.24f;

            if (dashCooldown > 0.0f) dashCooldown -= dt;
            if (dashStrikeFollowupTimer > 0.0f) dashStrikeFollowupTimer -= dt;
            if (inputBufferTimer > 0.0f) inputBufferTimer -= dt;
            if (comboResetTimer > 0.0f && comboStep == 0 && !isDashStrike) {
                comboResetTimer -= dt;
            }

            if (playerHp <= 0) {
                if (!playedDeathSound) {
                    audio.Play(audio.gameOverTone, 1.0f);
                    playedDeathSound = true;
                }
                if (IsKeyPressed(KEY_R)) {
                    playerMaxHp = 100;
                    playerHp = playerMaxHp;
                    playerPos = {screenW * 0.5f, screenH * 0.5f};
                    comboStep = 0;
                    isDashStrike = false;
                    attackTimer = 0.0f;
                    attackMaxTimer = 0.15f;
                    dashStrikeFollowupTimer = 0.0f;
                    isDashing = false;
                    afterimageSpawnTimer = 0.0f;
                    hitStopTimer = 0.0f;
                    choosingUpgrade = false;
                    choosingSuperUpgrade = false;
                    playedDeathSound = false;
                    droneOrbitAngle = 0.0f;
                    droneFireTimer = 0.0f;
                    build.Reset();
                    countersLanded = 0;
                    wave = 1;
                    score = 0;
                    popups.clear();
                    afterimages.clear();
                    sparks.clear();
                    pickups.clear();
                    enemyProjectiles.clear();
                    fireRings.clear();
                    droneBolts.clear();
                    SpawnWave(enemies, wave, screenW, screenH, audio);
                }
            } else {
                Vector2 moveInput = {0.0f, 0.0f};
                if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) moveInput.y -= 1.0f;
                if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) moveInput.y += 1.0f;
                if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) moveInput.x -= 1.0f;
                if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) moveInput.x += 1.0f;

                if (Vector2LengthSqr(moveInput) > 0.001f) {
                    moveInput = Vector2Normalize(moveInput);
                    if (!isDashing && comboStep == 0 && !isDashStrike) {
                        aimDir = moveInput;
                    }
                }

                Vector2 mousePos = GetMousePosition();
                Vector2 toMouse = Vector2Subtract(mousePos, playerPos);
                if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && Vector2LengthSqr(toMouse) > 1.0f) {
                    aimDir = Vector2Normalize(toMouse);
                }

                int axeRanks = build.GetSuper(SuperUpgradeId::LASER_AXE);
                int infernoRanks = build.GetSuper(SuperUpgradeId::INFERNO_THRUSTER);

                if ((IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_LEFT_SHIFT)) && !isDashing && dashCooldown <= 0.0f) {
                    isDashing = true;
                    isDashStrike = false;
                    comboStep = 0;
                    attackTimer = 0.0f;
                    dashTimer = dashDuration;
                    dashCooldown = dashCooldownMax;
                    afterimageSpawnTimer = 0.0f;
                    dashDir = (Vector2LengthSqr(moveInput) > 0.001f) ? moveInput : aimDir;
                    aimDir = dashDir;
                    for (Enemy& e : enemies) e.hitThisSwing = false;
                    SpawnGhost(afterimages, playerPos, aimDir, playerRadius);
                    audio.Play(audio.dashWhoosh, 1.05f);

                    if (infernoRanks > 0) {
                        audio.Play(audio.fireRing, 1.0f);
                        FireRingZone ring;
                        ring.pos = playerPos;
                        ring.radius = 76.0f + (infernoRanks - 1) * 14.0f;
                        ring.tickDamage = 10 + (infernoRanks - 1) * 6 + wave;
                        ring.tickTimer = 0.0f;
                        ring.tickInterval = 0.25f;
                        ring.timer = 0.0f;
                        ring.maxTimer = 1.90f;
                        fireRings.push_back(ring);
                    }
                }

                if (IsKeyPressed(KEY_J) || IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                    inputBufferTimer = inputBufferWindow;
                }

                if (inputBufferTimer > 0.0f && comboStep == 0 && !isDashStrike && (isDashing || dashStrikeFollowupTimer > 0.0f)) {
                    isDashing = false;
                    dashStrikeFollowupTimer = 0.0f;
                    isDashStrike = true;
                    attackMaxTimer = 0.18f;
                    attackTimer = attackMaxTimer;
                    comboResetTimer = 0.55f;
                    inputBufferTimer = 0.0f;
                    playerPos = Vector2Add(playerPos, Vector2Scale(aimDir, 32.0f));
                    SpawnGhost(afterimages, playerPos, aimDir, playerRadius);
                    for (Enemy& e : enemies) e.hitThisSwing = false;
                    audio.Play((axeRanks > 0) ? audio.axeSwing : audio.swing, 1.15f);
                } else if (inputBufferTimer > 0.0f && comboStep == 0 && !isDashing && !isDashStrike) {
                    int nextStep = (comboResetTimer > 0.0f) ? (comboStep + 1) : 1;
                    if (nextStep > 3) nextStep = 1;
                    comboStep = nextStep;
                    attackMaxTimer = (comboStep == 3) ? 0.22f : 0.15f;
                    attackTimer = attackMaxTimer;
                    comboResetTimer = 0.55f;
                    inputBufferTimer = 0.0f;

                    playerPos = Vector2Add(playerPos, Vector2Scale(aimDir, (comboStep == 3) ? 24.0f : 12.0f));
                    for (Enemy& e : enemies) e.hitThisSwing = false;
                    float pitch = (comboStep == 1) ? 0.96f : ((comboStep == 2) ? 1.06f : 0.86f);
                    audio.Play((axeRanks > 0) ? audio.axeSwing : audio.swing, pitch);
                }

                if (isDashing) {
                    playerPos = Vector2Add(playerPos, Vector2Scale(dashDir, dashSpeed * dt));
                    dashTimer -= dt;
                    afterimageSpawnTimer -= dt;
                    if (afterimageSpawnTimer <= 0.0f) {
                        SpawnGhost(afterimages, playerPos, aimDir, playerRadius);
                        afterimageSpawnTimer = afterimageSpawnInterval;
                    }
                    if (dashTimer <= 0.0f) {
                        isDashing = false;
                        dashStrikeFollowupTimer = 0.18f;
                        if (infernoRanks > 0) {
                            FireRingZone endRing;
                            endRing.pos = playerPos;
                            endRing.radius = 82.0f + (infernoRanks - 1) * 14.0f;
                            endRing.tickDamage = 11 + (infernoRanks - 1) * 6 + wave;
                            endRing.tickTimer = 0.0f;
                            endRing.tickInterval = 0.25f;
                            endRing.timer = 0.0f;
                            endRing.maxTimer = 1.90f;
                            fireRings.push_back(endRing);
                        }
                    }
                } else {
                    float speedMult = (comboStep > 0 || isDashStrike) ? 0.45f : 1.0f;
                    playerPos = Vector2Add(playerPos, Vector2Scale(moveInput, moveSpeed * speedMult * dt));
                }
                playerPos = ClampToScreen(playerPos, playerRadius, screenW, screenH);

                for (size_t i = 0; i < fireRings.size(); ) {
                    fireRings[i].timer += dt;
                    fireRings[i].tickTimer -= dt;

                    if (fireRings[i].tickTimer <= 0.0f) {
                        fireRings[i].tickTimer = fireRings[i].tickInterval;
                        for (Enemy& e : enemies) {
                            if (e.hp > 0 && CheckCollisionCircles(fireRings[i].pos, fireRings[i].radius, e.pos, e.radius)) {
                                e.hp -= fireRings[i].tickDamage;
                                e.hitFlashTimer = 0.06f;
                                SpawnHitSparks(sparks, e.pos, {0.0f, -1.0f}, 1, false);
                                DamagePopup burnPop;
                                burnPop.pos = {e.pos.x, e.pos.y - e.radius - 12.0f};
                                burnPop.amount = fireRings[i].tickDamage;
                                burnPop.timer = 0.0f;
                                burnPop.maxTimer = 0.55f;
                                burnPop.isFinisher = false;
                                burnPop.isCounter = false;
                                popups.push_back(burnPop);
                            }
                        }
                    }

                    if (fireRings[i].timer >= fireRings[i].maxTimer) {
                        fireRings.erase(fireRings.begin() + i);
                    } else {
                        ++i;
                    }
                }

                int droneRanks = build.GetSuper(SuperUpgradeId::DEFENSE_DRONE);
                droneOrbitAngle += 2.6f * dt;
                if (droneRanks > 0) {
                    droneFireTimer -= dt;
                    float fireInterval = fmaxf(0.24f, 0.52f - (droneRanks - 1) * 0.10f);
                    if (droneFireTimer <= 0.0f) {
                        bool firedAny = false;
                        int activeDrones = (droneRanks < 3) ? droneRanks : 3;
                        for (int d = 0; d < activeDrones; ++d) {
                            float a = droneOrbitAngle + (d * (2.0f * PI / (float)activeDrones));
                            Vector2 dPos = {
                                playerPos.x + cosf(a) * 44.0f,
                                playerPos.y + sinf(a) * 44.0f
                            };

                            const Enemy* closest = nullptr;
                            float bestDistSq = 1e18f;
                            for (const Enemy& e : enemies) {
                                if (e.hp > 0) {
                                    float distSq = Vector2LengthSqr(Vector2Subtract(e.pos, dPos));
                                    if (distSq < bestDistSq) {
                                        bestDistSq = distSq;
                                        closest = &e;
                                    }
                                }
                            }

                            if (closest != nullptr) {
                                Vector2 shotDir = Vector2Normalize(Vector2Subtract(closest->pos, dPos));
                                if (Vector2LengthSqr(shotDir) > 0.001f) {
                                    DroneBolt bolt;
                                    bolt.pos = dPos;
                                    bolt.vel = Vector2Scale(shotDir, 560.0f);
                                    bolt.damage = 16 + (droneRanks - 1) * 7 + (wave - 1) * 2;
                                    droneBolts.push_back(bolt);
                                    firedAny = true;
                                }
                            }
                        }
                        if (firedAny) {
                            audio.Play(audio.droneBolt, 0.96f + (float)GetRandomValue(0, 16) / 100.0f);
                        }
                        droneFireTimer = fireInterval;
                    }
                }

                for (size_t i = 0; i < droneBolts.size(); ) {
                    droneBolts[i].pos = Vector2Add(droneBolts[i].pos, Vector2Scale(droneBolts[i].vel, dt));
                    droneBolts[i].life -= dt;
                    bool hitEnemy = false;

                    for (Enemy& e : enemies) {
                        if (e.hp > 0 && CheckCollisionCircles(droneBolts[i].pos, droneBolts[i].radius, e.pos, e.radius)) {
                            e.hp -= droneBolts[i].damage;
                            e.hitFlashTimer = 0.065f;
                            hitEnemy = true;
                            Vector2 bDir = Vector2Normalize(droneBolts[i].vel);
                            SpawnHitSparks(sparks, droneBolts[i].pos, bDir, 1, false);

                            DamagePopup dPop;
                            dPop.pos = {e.pos.x, e.pos.y - e.radius - 10.0f};
                            dPop.amount = droneBolts[i].damage;
                            dPop.timer = 0.0f;
                            dPop.maxTimer = 0.55f;
                            dPop.isFinisher = false;
                            dPop.isCounter = false;
                            popups.push_back(dPop);
                            break;
                        }
                    }

                    if (hitEnemy || droneBolts[i].life <= 0.0f ||
                        droneBolts[i].pos.x < -20 || droneBolts[i].pos.x > screenW + 20 ||
                        droneBolts[i].pos.y < -20 || droneBolts[i].pos.y > screenH + 20) {
                        droneBolts.erase(droneBolts.begin() + i);
                    } else {
                        ++i;
                    }
                }

                for (size_t i = 0; i < pickups.size(); ) {
                    pickups[i].timer += dt;
                    bool expired = (pickups[i].timer >= pickups[i].maxTimer);
                    bool collected = false;

                    if (!expired && CheckCollisionCircles(playerPos, playerRadius + 6.0f, pickups[i].pos, pickups[i].radius)) {
                        if (playerHp < playerMaxHp) {
                            int actualHeal = pickups[i].healAmount;
                            if (playerHp + actualHeal > playerMaxHp) {
                                actualHeal = playerMaxHp - playerHp;
                            }
                            playerHp += actualHeal;
                            collected = true;
                            audio.Play(audio.healPickup, 1.05f);

                            DamagePopup hpPop;
                            hpPop.pos = {playerPos.x, playerPos.y - playerRadius - 12.0f};
                            hpPop.amount = actualHeal;
                            hpPop.timer = 0.0f;
                            hpPop.maxTimer = 0.75f;
                            hpPop.isFinisher = false;
                            hpPop.isCounter = false;
                            hpPop.isHeal = true;
                            popups.push_back(hpPop);
                        }
                    }

                    if (expired || collected) {
                        pickups.erase(pickups.begin() + i);
                    } else {
                        ++i;
                    }
                }

                if (comboStep > 0 || isDashStrike) {
                    float reach = (isDashStrike ? 92.0f : ((comboStep == 3) ? 80.0f : 64.0f)) + axeRanks * 14.0f;
                    float hitRadius = (isDashStrike ? 42.0f : ((comboStep == 3) ? 48.0f : 35.0f)) + axeRanks * 11.0f;
                    Vector2 slashCenter = Vector2Add(playerPos, Vector2Scale(aimDir, reach * 0.65f));

                    int tempoBonus = build.Get(PowerUpId::BLADE_TEMPO) * 6;
                    int dashStrikeBonus = build.Get(PowerUpId::TEMPEST_DASH) * 12;
                    int axeDmgBonus = axeRanks * 14;
                    float finisherMult = 1.0f + 0.25f * (float)build.Get(PowerUpId::HEAVY_IMPACT);

                    int baseDmg = isDashStrike ? (24 + dashStrikeBonus + tempoBonus + axeDmgBonus)
                                  : ((comboStep == 1) ? (15 + tempoBonus + axeDmgBonus)
                                  : ((comboStep == 2) ? (20 + tempoBonus + axeDmgBonus)
                                  : (int)roundf((34 + tempoBonus + axeDmgBonus) * finisherMult)));

                    float knockback = (isDashStrike ? 310.0f
                                     : ((comboStep == 3) ? (380.0f * (1.0f + 0.20f * build.Get(PowerUpId::HEAVY_IMPACT)))
                                     : 150.0f)) * (1.0f + 0.15f * axeRanks);
                    float staggerDur = isDashStrike ? 0.24f : ((comboStep == 3) ? 0.28f : 0.14f);
                    bool landedHitThisFrame = false;
                    bool brokeWindupThisFrame = false;

                    for (Enemy& e : enemies) {
                        if (e.hp > 0 && !e.hitThisSwing) {
                            if (CheckCollisionCircles(slashCenter, hitRadius, e.pos, e.radius)) {
                                e.hitThisSwing = true;
                                landedHitThisFrame = true;
                                bool brokeWindup = (e.state == EnemyState::WINDUP);
                                bool isFinisherHit = (comboStep == 3) || isDashStrike;
                                if (brokeWindup) brokeWindupThisFrame = true;

                                int dmg = baseDmg + (wave - 1) * 2;
                                float usedKnockback = knockback;
                                float usedStagger = staggerDur;

                                if (brokeWindup) {
                                    float counterMult = 1.45f + 0.22f * (float)build.Get(PowerUpId::COUNTER_SURGE);
                                    dmg = (int)roundf(dmg * counterMult);
                                    usedKnockback += 110.0f;
                                    usedStagger += 0.12f;
                                    countersLanded++;

                                    int counterHeal = build.Get(PowerUpId::COUNTER_SURGE) * 4;
                                    if (counterHeal > 0 && playerHp < playerMaxHp) {
                                        int healed = (int)fminf((float)(playerMaxHp - playerHp), (float)counterHeal);
                                        playerHp += healed;
                                    }

                                    int scoreGain = (int)roundf((50 * wave) * (1.0f + 0.10f * build.Get(PowerUpId::SPARK_VAMPIRISM)));
                                    score += scoreGain;
                                }

                                if ((brokeWindup || isFinisherHit) && build.Get(PowerUpId::SPARK_VAMPIRISM) > 0 && playerHp < playerMaxHp) {
                                    int vampHeal = build.Get(PowerUpId::SPARK_VAMPIRISM);
                                    int healed = (int)fminf((float)(playerMaxHp - playerHp), (float)vampHeal);
                                    playerHp += healed;
                                }

                                Vector2 kbDir = Vector2Normalize(Vector2Subtract(e.pos, playerPos));
                                if (Vector2LengthSqr(kbDir) < 0.001f) kbDir = aimDir;

                                Vector2 impactPt = Vector2Lerp(e.pos, playerPos, e.radius / (e.radius + playerRadius + 4.0f));
                                SpawnHitSparks(sparks, impactPt, kbDir, isDashStrike ? 2 : comboStep, brokeWindup);

                                ApplyHitToEnemy(e, dmg, kbDir, usedKnockback, usedStagger, brokeWindup);

                                DamagePopup p;
                                p.pos = {e.pos.x, e.pos.y - e.radius - 10.0f};
                                p.amount = dmg;
                                p.timer = 0.0f;
                                p.maxTimer = brokeWindup ? 0.85f : 0.65f;
                                p.isFinisher = isFinisherHit;
                                p.isCounter = brokeWindup;
                                popups.push_back(p);

                                float shakeT = (comboStep == 3 || e.type == EnemyType::COMMANDER) ? 0.16f : 0.08f;
                                float shakeM = (e.type == EnemyType::COMMANDER) ? 8.0f : ((comboStep == 3) ? 7.0f : 3.5f);
                                if (brokeWindup) {
                                    shakeT = fmaxf(shakeT, 0.16f);
                                    shakeM = fmaxf(shakeM, 8.0f);
                                }
                                screenShakeTimer = shakeT;
                                screenShakeMag = shakeM;
                            }
                        }
                    }

                    if (landedHitThisFrame) {
                        if (brokeWindupThisFrame) {
                            audio.Play(audio.counterChime, 1.04f);
                        } else if (comboStep == 3 || isDashStrike) {
                            audio.Play(audio.hitHeavy, 0.98f);
                        } else {
                            audio.Play(audio.hitLight, 0.95f + (float)GetRandomValue(0, 14) / 100.0f);
                        }

                        float stopDuration = (comboStep == 3) ? 0.065f : 0.032f;
                        if (brokeWindupThisFrame) {
                            stopDuration = fmaxf(stopDuration, 0.070f);
                        }
                        if (stopDuration > hitStopTimer) {
                            hitStopTimer = stopDuration;
                        }
                    }

                    attackTimer -= dt;
                    if (attackTimer <= 0.0f) {
                        if (isDashStrike) {
                            isDashStrike = false;
                            for (Enemy& e : enemies) e.hitThisSwing = false;
                            if (inputBufferTimer > 0.0f) {
                                comboStep = 2;
                                attackMaxTimer = 0.15f;
                                attackTimer = attackMaxTimer;
                                comboResetTimer = 0.55f;
                                inputBufferTimer = 0.0f;
                                playerPos = Vector2Add(playerPos, Vector2Scale(aimDir, 12.0f));
                                audio.Play((axeRanks > 0) ? audio.axeSwing : audio.swing, 1.06f);
                            }
                        } else if (inputBufferTimer > 0.0f) {
                            comboStep = (comboStep % 3) + 1;
                            attackMaxTimer = (comboStep == 3) ? 0.22f : 0.15f;
                            attackTimer = attackMaxTimer;
                            comboResetTimer = 0.55f;
                            inputBufferTimer = 0.0f;
                            playerPos = Vector2Add(playerPos, Vector2Scale(aimDir, (comboStep == 3) ? 24.0f : 12.0f));
                            for (Enemy& e : enemies) e.hitThisSwing = false;
                            float pitch = (comboStep == 1) ? 0.96f : ((comboStep == 2) ? 1.06f : 0.86f);
                            audio.Play((axeRanks > 0) ? audio.axeSwing : audio.swing, pitch);
                        } else {
                            comboStep = 0;
                            for (Enemy& e : enemies) e.hitThisSwing = false;
                        }
                    }
                }

                for (size_t i = 0; i < enemyProjectiles.size(); ) {
                    enemyProjectiles[i].pos = Vector2Add(enemyProjectiles[i].pos, Vector2Scale(enemyProjectiles[i].vel, dt));
                    enemyProjectiles[i].life -= dt;
                    bool hitPlayer = false;

                    if (!isDashing &&
                        CheckCollisionCircles(enemyProjectiles[i].pos, enemyProjectiles[i].radius, playerPos, playerRadius)) {
                        playerHp -= enemyProjectiles[i].damage;
                        if (playerHp < 0) playerHp = 0;
                        hitPlayer = true;
                        screenShakeTimer = 0.15f;
                        screenShakeMag = 6.0f;
                        audio.Play(audio.hitLight, 0.78f);
                        SpawnHitSparks(sparks, playerPos, Vector2Normalize(enemyProjectiles[i].vel), 1, false);
                    }

                    if (hitPlayer || enemyProjectiles[i].life <= 0.0f ||
                        enemyProjectiles[i].pos.x < -24 || enemyProjectiles[i].pos.x > screenW + 24 ||
                        enemyProjectiles[i].pos.y < -24 || enemyProjectiles[i].pos.y > screenH + 24) {
                        enemyProjectiles.erase(enemyProjectiles.begin() + i);
                    } else {
                        ++i;
                    }
                }

                activeAttackers = 0;
                for (const Enemy& other : enemies) {
                    if (other.hp > 0 && (other.state == EnemyState::WINDUP || other.state == EnemyState::ATTACK)) {
                        activeAttackers++;
                    }
                }

                for (size_t i = 0; i < enemies.size(); ++i) {
                    Enemy& e = enemies[i];
                    if (e.hp <= 0) continue;

                    if (e.hitFlashTimer > 0.0f) {
                        e.hitFlashTimer -= dt;
                    }
                    if (e.attackCooldown > 0.0f) {
                        e.attackCooldown -= dt;
                    }

                    Vector2 separation = {0.0f, 0.0f};
                    for (size_t j = 0; j < enemies.size(); ++j) {
                        if (i == j || enemies[j].hp <= 0) continue;
                        Vector2 diff = Vector2Subtract(e.pos, enemies[j].pos);
                        float d = Vector2Length(diff);
                        float minSep = e.radius + enemies[j].radius + 24.0f;
                        if (d > 0.001f && d < minSep) {
                            float push = (minSep - d) / minSep;
                            separation = Vector2Add(separation, Vector2Scale(Vector2Scale(diff, 1.0f / d), push * 175.0f));
                        }
                    }

                    Vector2 toPlayer = Vector2Subtract(playerPos, e.pos);
                    float dist = Vector2Length(toPlayer);
                    Vector2 dir = (dist > 0.001f) ? Vector2Scale(toPlayer, 1.0f / dist) : Vector2{1.0f, 0.0f};

                    switch (e.state) {
                        case EnemyState::CHASE: {
                            Vector2 moveDir = dir;
                            float desiredRingDist = e.isRanged ? (e.triggerRange * 0.68f) : (e.triggerRange * 0.9f);

                            if (e.isRanged) {
                                if (dist < desiredRingDist * 0.65f) {
                                    moveDir = Vector2Scale(dir, -0.75f);
                                } else if (dist < desiredRingDist + 50.0f) {
                                    Vector2 perp = {-dir.y * e.flankSign, dir.x * e.flankSign};
                                    moveDir = Vector2Normalize(Vector2Add(Vector2Scale(perp, 0.88f), Vector2Scale(dir, 0.15f)));
                                } else if (e.type == EnemyType::FLANKER) {
                                    Vector2 perp = {-dir.y * e.flankSign, dir.x * e.flankSign};
                                    moveDir = Vector2Normalize(Vector2Add(Vector2Scale(dir, 0.65f), Vector2Scale(perp, 0.75f)));
                                }
                            } else {
                                if (activeAttackers >= maxSimultaneousAttackers && dist < desiredRingDist + 75.0f) {
                                    Vector2 perp = {-dir.y * e.flankSign, dir.x * e.flankSign};
                                    moveDir = Vector2Normalize(Vector2Add(Vector2Scale(perp, 0.9f), Vector2Scale(dir, 0.1f)));
                                } else if (e.type == EnemyType::FLANKER && dist > desiredRingDist && dist < 280.0f) {
                                    Vector2 perp = {-dir.y * e.flankSign, dir.x * e.flankSign};
                                    moveDir = Vector2Normalize(Vector2Add(Vector2Scale(dir, 0.58f), Vector2Scale(perp, 0.82f)));
                                }
                            }

                            e.pos = Vector2Add(e.pos, Vector2Scale(moveDir, e.speed * dt));
                            e.pos = Vector2Add(e.pos, Vector2Scale(separation, dt));

                            if (dist <= e.triggerRange && e.attackCooldown <= 0.0f && activeAttackers < maxSimultaneousAttackers) {
                                e.state = EnemyState::WINDUP;
                                e.stateTimer = e.windupDuration;
                                e.attackDir = dir;
                                e.hasDealtDamage = false;
                                activeAttackers++;
                            }
                            break;
                        }
                        case EnemyState::WINDUP: {
                            e.stateTimer -= dt;
                            if (e.isRanged) {
                                if (dist > 0.001f) {
                                    e.attackDir = Vector2Lerp(e.attackDir, dir, 6.0f * dt);
                                    e.attackDir = Vector2Normalize(e.attackDir);
                                }
                                e.pos = Vector2Add(e.pos, Vector2Scale(separation, 0.25f * dt));
                            } else {
                                float pullBack = (e.type == EnemyType::COMMANDER) ? 12.0f
                                               : ((e.type == EnemyType::BRUISER) ? 16.0f : 24.0f);
                                e.pos = Vector2Subtract(e.pos, Vector2Scale(e.attackDir, pullBack * dt));
                                e.pos = Vector2Add(e.pos, Vector2Scale(separation, 0.45f * dt));
                            }

                            if (e.stateTimer <= 0.0f) {
                                e.state = EnemyState::ATTACK;
                                e.stateTimer = e.attackDuration;
                                e.hasDealtDamage = false;

                                if (e.isRanged) {
                                    EnemyProjectile proj;
                                    proj.pos = Vector2Add(e.pos, Vector2Scale(e.attackDir, e.radius + 8.0f));
                                    float boltSpeed = (e.type == EnemyType::BRUISER) ? 260.0f
                                                    : ((e.type == EnemyType::FLANKER) ? 330.0f : 300.0f);
                                    proj.vel = Vector2Scale(e.attackDir, boltSpeed);
                                    proj.radius = (e.type == EnemyType::BRUISER) ? 9.0f : 6.5f;
                                    proj.damage = (e.type == EnemyType::BRUISER) ? 16 : 11;
                                    proj.color = (e.type == EnemyType::BRUISER) ? GOLD : MAGENTA;
                                    proj.life = 3.2f;
                                    enemyProjectiles.push_back(proj);
                                    e.hasDealtDamage = true;
                                    audio.Play(audio.droneBolt, 0.68f);
                                }
                            }
                            break;
                        }
                        case EnemyState::ATTACK: {
                            e.stateTimer -= dt;
                            if (!e.isRanged) {
                                e.pos = Vector2Add(e.pos, Vector2Scale(e.attackDir, e.lungeSpeed * dt));
                                int enemyDmg = (e.type == EnemyType::COMMANDER) ? 26
                                               : ((e.type == EnemyType::BRUISER) ? 18 : 10);
                                float hitExpand = (e.type == EnemyType::COMMANDER) ? 16.0f
                                                : ((e.type == EnemyType::BRUISER) ? 12.0f : 6.0f);

                                if (!isDashing && !e.hasDealtDamage &&
                                    CheckCollisionCircles(e.pos, e.radius + hitExpand, playerPos, playerRadius)) {
                                    playerHp -= enemyDmg;
                                    if (playerHp < 0) playerHp = 0;
                                    e.hasDealtDamage = true;
                                    screenShakeTimer = (e.type == EnemyType::COMMANDER) ? 0.26f
                                                     : ((e.type == EnemyType::BRUISER) ? 0.22f : 0.14f);
                                    screenShakeMag = (e.type == EnemyType::COMMANDER) ? 11.0f
                                                   : ((e.type == EnemyType::BRUISER) ? 9.0f : 5.5f);
                                    audio.Play(audio.hitHeavy, 0.80f);
                                }
                            }
                            if (e.stateTimer <= 0.0f) {
                                e.state = EnemyState::RECOVERY;
                                e.stateTimer = e.recoveryDuration;
                                e.attackCooldown = e.isRanged
                                                 ? ((float)GetRandomValue(95, 155) / 100.0f)
                                                 : ((float)GetRandomValue(55, 115) / 100.0f);
                            }
                            break;
                        }
                        case EnemyState::RECOVERY: {
                            e.stateTimer -= dt;
                            e.pos = Vector2Add(e.pos, Vector2Scale(separation, 0.6f * dt));
                            if (e.stateTimer <= 0.0f) {
                                e.state = EnemyState::CHASE;
                            }
                            break;
                        }
                        case EnemyState::STAGGER: {
                            e.stateTimer -= dt;
                            e.pos = Vector2Add(e.pos, Vector2Scale(e.vel, dt));
                            e.pos = Vector2Add(e.pos, Vector2Scale(separation, 0.5f * dt));
                            e.vel = Vector2Scale(e.vel, powf(0.001f, dt));
                            if (e.stateTimer <= 0.0f) {
                                e.vel = {0.0f, 0.0f};
                                e.state = EnemyState::CHASE;
                            }
                            break;
                        }
                    }

                    e.pos = ClampToScreen(e.pos, e.radius, screenW, screenH);
                }

                int aliveCount = 0;
                for (size_t i = 0; i < enemies.size(); ) {
                    if (enemies[i].hp <= 0) {
                        bool isCommander = (enemies[i].type == EnemyType::COMMANDER);
                        int baseKillScore = isCommander ? 600
                                          : ((enemies[i].type == EnemyType::BRUISER) ? 250 : 120);
                        baseKillScore *= wave;
                        score += (int)roundf(baseKillScore * (1.0f + 0.10f * build.Get(PowerUpId::SPARK_VAMPIRISM)));

                        int waveBonus = (wave - 1) * 3;
                        if (waveBonus > 15) waveBonus = 15;

                        int hpRatioPct = (playerMaxHp > 0) ? ((playerHp * 100) / playerMaxHp) : 100;
                        int lowHpBonus = (hpRatioPct < 35) ? 22 : ((hpRatioPct < 65) ? 14 : 0);
                        int vitalityBonus = build.Get(PowerUpId::VITALITY_SURGE) * 6;

                        int dropChance = 26 + waveBonus + lowHpBonus + vitalityBonus;
                        if (dropChance > 78) dropChance = 78;

                        if (isCommander || GetRandomValue(0, 99) < dropChance) {
                            HealthPickup orb;
                            orb.pos = ClampToScreen(enemies[i].pos, orb.radius, screenW, screenH);
                            int baseHeal = 18 + (wave - 1) * 2;
                            if (baseHeal > 28) baseHeal = 28;
                            orb.healAmount = isCommander ? (baseHeal + 12)
                                           : ((enemies[i].type == EnemyType::BRUISER) ? (baseHeal + 6) : baseHeal);
                            pickups.push_back(orb);
                        }

                        enemies.erase(enemies.begin() + i);
                    } else {
                        ++aliveCount;
                        ++i;
                    }
                }

                if (aliveCount == 0 && !choosingUpgrade && !choosingSuperUpgrade) {
                    if (wave > bestWave) bestWave = wave;
                    if (score > bestScore) bestScore = score;
                    if (wave % 5 == 0) {
                        RollSuperChoices(superChoices);
                        choosingSuperUpgrade = true;
                    } else {
                        RollDraftChoices(draftChoices);
                        choosingUpgrade = true;
                    }
                }
            }
        }

        Vector2 shakeOffset = {0.0f, 0.0f};
        if (screenShakeTimer > 0.0f) {
            shakeOffset.x = (float)GetRandomValue(-100, 100) / 100.0f * screenShakeMag;
            shakeOffset.y = (float)GetRandomValue(-100, 100) / 100.0f * screenShakeMag;
        }

        BeginDrawing();
        ClearBackground(Color{16, 20, 32, 255});

        for (int x = 0; x < screenW; x += 64) {
            DrawLine(x + (int)shakeOffset.x, 0, x + (int)shakeOffset.x, screenH, Color{26, 33, 52, 255});
        }
        for (int y = 0; y < screenH; y += 64) {
            DrawLine(0, y + (int)shakeOffset.y, screenW, y + (int)shakeOffset.y, Color{26, 33, 52, 255});
        }
        Vector2 arenaCenter = {screenW * 0.5f + shakeOffset.x, screenH * 0.5f + shakeOffset.y};
        DrawCircleLines((int)arenaCenter.x, (int)arenaCenter.y, 130.0f, Color{30, 42, 66, 255});
        DrawCircleLines((int)arenaCenter.x, (int)arenaCenter.y, 250.0f, Color{26, 36, 58, 255});

        for (const FireRingZone& ring : fireRings) {
            float lifeRatio = Clamp(1.0f - (ring.timer / ring.maxTimer), 0.0f, 1.0f);
            Vector2 rPos = Vector2Add(ring.pos, shakeOffset);
            float pulse = 1.0f + 0.05f * sinf(ring.timer * 14.0f);
            float r = ring.radius * pulse;
            DrawCircleV(rPos, r, Fade(ORANGE, 0.14f * lifeRatio));
            DrawCircleLines((int)rPos.x, (int)rPos.y, r, Fade(ORANGE, 0.85f * lifeRatio));
            DrawCircleLines((int)rPos.x, (int)rPos.y, r - 6.0f, Fade(GOLD, 0.70f * lifeRatio));
        }

        for (const HealthPickup& orb : pickups) {
            float remaining = orb.maxTimer - orb.timer;
            float alpha = (remaining < 2.0f) ? Clamp(remaining / 2.0f, 0.25f, 1.0f) : 1.0f;
            float pulse = 1.0f + 0.14f * sinf(orb.timer * 7.0f);
            Vector2 oPos = Vector2Add(orb.pos, shakeOffset);
            float r = orb.radius * pulse;

            DrawCircleV(oPos, r + 4.0f, Fade(GREEN, alpha * 0.28f));
            DrawCircleV(oPos, r, Fade(Color{65, 225, 115, 255}, alpha));
            DrawCircleLines((int)oPos.x, (int)oPos.y, r, Fade(RAYWHITE, alpha));
            DrawLineEx({oPos.x - 4.0f, oPos.y}, {oPos.x + 4.0f, oPos.y}, 2.5f, Fade(RAYWHITE, alpha));
            DrawLineEx({oPos.x, oPos.y - 4.0f}, {oPos.x, oPos.y + 4.0f}, 2.5f, Fade(RAYWHITE, alpha));
        }

        for (const DashAfterimage& g : afterimages) {
            float lifeRatio = Clamp(1.0f - (g.timer / g.maxTimer), 0.0f, 1.0f);
            float ghostRadius = g.radius * (0.85f + 0.15f * lifeRatio);
            Vector2 gPos = Vector2Add(g.pos, shakeOffset);
            DrawExoSuitDefender(gPos, ghostRadius, g.aimDir,
                                Fade(SKYBLUE, lifeRatio * 0.32f),
                                Fade(WHITE, lifeRatio * 0.45f),
                                Fade(YELLOW, lifeRatio * 0.45f),
                                false, g.timer);
        }

        for (const Enemy& e : enemies) {
            Vector2 drawPos = Vector2Add(e.pos, shakeOffset);
            Color baseColor = (e.type == EnemyType::COMMANDER) ? Color{125, 22, 82, 255}
                            : (e.isRanged ? Color{92, 34, 128, 255}
                            : ((e.type == EnemyType::BRUISER) ? Color{155, 36, 68, 255}
                            : ((e.type == EnemyType::FLANKER) ? Color{195, 86, 38, 255}
                            : Color{150, 38, 52, 255})));
            Color trimColor = (e.type == EnemyType::COMMANDER) ? GOLD
                            : (e.isRanged ? MAGENTA
                            : ((e.type == EnemyType::BRUISER) ? GOLD
                            : ((e.type == EnemyType::FLANKER) ? ORANGE : PINK)));
            Color bodyColor = baseColor;
            float drawRadius = e.radius;
            Vector2 faceDir = (e.state == EnemyState::WINDUP || e.state == EnemyState::ATTACK)
                              ? e.attackDir
                              : Vector2Normalize(Vector2Subtract(playerPos, e.pos));
            if (Vector2LengthSqr(faceDir) < 0.001f) faceDir = {0.0f, 1.0f};

            if (e.state == EnemyState::WINDUP) {
                float flashSpeed = (e.type == EnemyType::COMMANDER) ? 18.0f
                                 : ((e.type == EnemyType::SKIRMISHER) ? 26.0f : 20.0f);
                bodyColor = ((int)(e.stateTimer * flashSpeed) % 2 == 0) ? GOLD : ORANGE;
                trimColor = YELLOW;
                drawRadius = e.radius * 1.15f;
                float teleLen = e.isRanged ? 165.0f
                              : ((e.type == EnemyType::COMMANDER) ? 120.0f
                              : ((e.type == EnemyType::BRUISER) ? 95.0f : 72.0f));
                Vector2 telegraphEnd = Vector2Add(drawPos, Vector2Scale(e.attackDir, teleLen));
                DrawLineEx(drawPos, telegraphEnd, e.isRanged ? 2.5f
                                                 : ((e.type == EnemyType::COMMANDER) ? 5.5f : 3.5f),
                           Fade(e.isRanged ? MAGENTA : YELLOW, 0.88f));
            } else if (e.state == EnemyState::ATTACK) {
                bodyColor = RED;
                trimColor = YELLOW;
            } else if (e.state == EnemyState::STAGGER) {
                bodyColor = RAYWHITE;
                trimColor = SKYBLUE;
            }

            if (e.hitFlashTimer > 0.0f) {
                bodyColor = RAYWHITE;
                trimColor = WHITE;
            }

            DrawCrabSilhouette(drawPos, drawRadius, e, bodyColor, trimColor, faceDir);

            float hpRatio = Clamp((float)e.hp / (float)e.maxHp, 0.0f, 1.0f);
            float barW = (e.type == EnemyType::COMMANDER) ? 62.0f
                         : ((e.type == EnemyType::BRUISER) ? 44.0f : 32.0f);
            float barH = (e.type == EnemyType::COMMANDER) ? 7.0f : 5.0f;
            float barX = drawPos.x - barW * 0.5f;
            float barY = drawPos.y - drawRadius - 15.0f;
            DrawRectangle((int)barX, (int)barY, (int)barW, (int)barH, BLACK);
            DrawRectangle((int)barX, (int)barY, (int)(barW * hpRatio), (int)barH,
                          (e.type == EnemyType::COMMANDER) ? GOLD : (e.isRanged ? MAGENTA : RED));

            int labelFont = (e.type == EnemyType::COMMANDER) ? 12 : 10;
            Color labelColor = (e.type == EnemyType::COMMANDER) ? GOLD : (e.isRanged ? MAGENTA : RAYWHITE);
            int labelW = MeasureText(e.name, labelFont);
            DrawText(e.name, (int)drawPos.x - labelW / 2, (int)barY - (labelFont + 2), labelFont, labelColor);

            if (debugOverlay) {
                DrawCircleLines((int)drawPos.x, (int)drawPos.y, e.triggerRange, Fade(ORANGE, 0.45f));
                DrawCircleLines((int)drawPos.x, (int)drawPos.y, e.radius + 12.0f, Fade(SKYBLUE, 0.45f));

                if (Vector2LengthSqr(e.vel) > 1.0f) {
                    Vector2 velEnd = Vector2Add(drawPos, Vector2Scale(e.vel, 0.12f));
                    DrawLineEx(drawPos, velEnd, 2.0f, LIME);
                }

                const char* stName = EnemyStateLabel(e.state);
                DrawText(TextFormat("%s %s %.2f", EnemyArchetypeName(e), stName, e.stateTimer),
                         (int)drawPos.x - 42, (int)barY - 26, 11, YELLOW);
                DrawText(TextFormat("cd:%.2f", e.attackCooldown), (int)drawPos.x - 20, (int)barY - 14, 11, SKYBLUE);
            }
        }

        for (const EnemyProjectile& proj : enemyProjectiles) {
            Vector2 bPos = Vector2Add(proj.pos, shakeOffset);
            DrawCircleV(bPos, proj.radius + 3.0f, Fade(proj.color, 0.30f));
            DrawCircleV(bPos, proj.radius, proj.color);
            DrawCircleLines((int)bPos.x, (int)bPos.y, proj.radius, RAYWHITE);
        }

        Vector2 pDraw = Vector2Add(playerPos, shakeOffset);
        int axeRanks = build.GetSuper(SuperUpgradeId::LASER_AXE);
        if (isDashStrike || comboStep > 0) {
            float reach = (isDashStrike ? 92.0f : ((comboStep == 3) ? 80.0f : 64.0f)) + axeRanks * 14.0f;
            float hitRadius = (isDashStrike ? 42.0f : ((comboStep == 3) ? 48.0f : 35.0f)) + axeRanks * 11.0f;
            Vector2 slashCenter = Vector2Add(pDraw, Vector2Scale(aimDir, reach * 0.65f));

            float sweepProgress = (attackMaxTimer > 0.001f)
                                  ? Clamp(1.0f - (attackTimer / attackMaxTimer), 0.0f, 1.0f)
                                  : 1.0f;
            float fadeOut = Clamp(attackTimer / fmaxf(attackMaxTimer * 0.55f, 0.04f), 0.25f, 1.0f);

            float innerR = playerRadius + (isDashStrike ? 8.0f : 4.0f);
            float outerR = innerR + (isDashStrike ? 54.0f : ((comboStep == 3) ? 52.0f : 40.0f)) + axeRanks * 16.0f;
            float halfArc = (isDashStrike ? 0.68f : ((comboStep == 3) ? 1.15f : 0.92f)) + axeRanks * 0.42f;
            if (halfArc > 2.35f) halfArc = 2.35f;
            bool flipSweep = (comboStep == 2);

            Color fillColor = (axeRanks > 0)
                            ? Fade(GOLD, 0.36f * fadeOut)
                            : (isDashStrike ? Fade(LIME, 0.36f * fadeOut)
                            : ((comboStep == 3) ? Fade(GOLD, 0.40f * fadeOut)
                            : Fade(SKYBLUE, 0.32f * fadeOut)));
            Color edgeColor = (axeRanks > 0)
                            ? Fade(ORANGE, 0.95f * fadeOut)
                            : (isDashStrike ? Fade(RAYWHITE, 0.90f * fadeOut)
                            : ((comboStep == 3) ? Fade(GOLD, 0.95f * fadeOut)
                            : Fade(WHITE, 0.85f * fadeOut)));

            DrawSweepingSlashArc(pDraw, aimDir, innerR, outerR, halfArc, sweepProgress, flipSweep, fillColor, edgeColor);

            if (debugOverlay) {
                DrawCircleLines((int)slashCenter.x, (int)slashCenter.y, hitRadius, LIME);
            }
        }

        Color armorColor    = isDashing ? Color{95, 195, 240, 255}
                            : (isDashStrike ? Color{85, 205, 165, 255} : Color{52, 132, 210, 255});
        Color pauldronColor = (axeRanks > 0) ? GOLD : Color{115, 195, 255, 255};
        Color visorColor    = isDashing ? YELLOW : Color{150, 245, 255, 255};
        DrawExoSuitDefender(pDraw, playerRadius, aimDir, armorColor, pauldronColor, visorColor,
                            isDashing, (float)GetTime());
        if (debugOverlay) {
            DrawCircleLines((int)pDraw.x, (int)pDraw.y, playerRadius, isDashing ? LIME : SKYBLUE);
        }

        int droneRanks = build.GetSuper(SuperUpgradeId::DEFENSE_DRONE);
        if (droneRanks > 0) {
            int activeDrones = (droneRanks < 3) ? droneRanks : 3;
            for (int d = 0; d < activeDrones; ++d) {
                float a = droneOrbitAngle + (d * (2.0f * PI / (float)activeDrones));
                Vector2 dPos = {
                    pDraw.x + cosf(a) * 44.0f,
                    pDraw.y + sinf(a) * 44.0f
                };
                DrawCircleV(dPos, 8.0f, SKYBLUE);
                DrawCircleLines((int)dPos.x, (int)dPos.y, 8.0f, WHITE);
                DrawCircleV(dPos, 3.0f, YELLOW);
            }
        }

        for (const DroneBolt& bolt : droneBolts) {
            Vector2 bPos = Vector2Add(bolt.pos, shakeOffset);
            Vector2 bTail = Vector2Subtract(bPos, Vector2Scale(bolt.vel, 0.025f));
            DrawLineEx(bTail, bPos, 3.5f, SKYBLUE);
            DrawCircleV(bPos, bolt.radius, RAYWHITE);
        }

        for (const HitSpark& s : sparks) {
            float lifeRatio = Clamp(1.0f - (s.timer / s.maxTimer), 0.0f, 1.0f);
            Vector2 sPos = Vector2Add(s.pos, shakeOffset);
            Vector2 sTail = Vector2Subtract(sPos, Vector2Scale(s.vel, 0.03f));
            DrawLineEx(sTail, sPos, fmaxf(2.0f, s.size * 0.65f * lifeRatio), Fade(s.color, lifeRatio));
            DrawCircleV(sPos, fmaxf(1.5f, s.size * 0.45f * lifeRatio), Fade(RAYWHITE, lifeRatio));
        }

        for (const DamagePopup& p : popups) {
            float alpha = 1.0f - (p.timer / p.maxTimer);
            char buf[24];
            if (p.isHeal) {
                snprintf(buf, sizeof(buf), "+%d HP", p.amount);
                Vector2 pos = Vector2Add(p.pos, shakeOffset);
                DrawText(buf, (int)pos.x - 18, (int)pos.y, 18, Fade(LIME, alpha));
            } else {
                Color c = p.isCounter ? Fade(LIME, alpha)
                        : (p.isFinisher ? Fade(GOLD, alpha) : Fade(WHITE, alpha));
                snprintf(buf, sizeof(buf), "%d", p.amount);
                int fontSize = (p.isFinisher || p.isCounter) ? 24 : 18;
                Vector2 pos = Vector2Add(p.pos, shakeOffset);
                if (p.isCounter) {
                    DrawText("COUNTER!", (int)pos.x - 28, (int)pos.y - 16, 13, Fade(GOLD, alpha));
                }
                DrawText(buf, (int)pos.x - 10, (int)pos.y, fontSize, c);
            }
        }

        const char* waveTag = (wave % 5 == 0) ? " [COMMANDER WAVE!]" : "";
        const char* muteTag = audio.muted ? " [MUTED]" : "";
        DrawText(TextFormat("THE ABYSSAL CLAW INVASION!%s  |  Wave: %d  Score: %d  Best: %d  Counters: %d%s",
                            waveTag, wave, score, bestWave, countersLanded, muteTag), 16, 12, 18,
                 (wave % 5 == 0) ? GOLD : RAYWHITE);
        DrawText(TextFormat("SHIELD/HULL HP: %d / %d", playerHp, playerMaxHp), 16, 36, 20, (playerHp > 30) ? GREEN : RED);
        std::string buildHud = "Loadout: " + FormatBuildSummary(build);
        DrawText(buildHud.c_str(), 16, 62, 14, GOLD);
        DrawText("WASD: Move | J / Left Click: Slash / Dash-Strike | Space / Shift: Dash | M: Mute | F1: Debug",
                 16, screenH - 28, 15, LIGHTGRAY);

        if (debugOverlay) {
            DrawRectangle(screenW - 255, 12, 240, 82, Fade(BLACK, 0.72f));
            DrawRectangleLines(screenW - 255, 12, 240, 82, LIME);
            DrawText("[F1 DEBUG OVERLAY: ON]", screenW - 245, 20, 14, LIME);
            DrawText(TextFormat("Attackers: %d / %d", activeAttackers, maxSimultaneousAttackers), screenW - 245, 40, 14, RAYWHITE);
            DrawText(TextFormat("HitStop: %.3fs  Combo: %d", hitStopTimer, comboStep), screenW - 245, 58, 14, GOLD);
            DrawText(TextFormat("Dash: %s  CD: %.2f", isDashing ? "IFRAME" : (isDashStrike ? "STRIKE" : "READY"), dashCooldown), screenW - 245, 75, 13, SKYBLUE);
        }

        if (choosingSuperUpgrade && playerHp > 0) {
            DrawRectangle(0, 0, screenW, screenH, Fade(BLACK, 0.78f));
            DrawText(TextFormat("COMMANDER SLAIN — WAVE %d SUPER-UPGRADE", wave),
                     screenW / 2 - 285, screenH / 2 - 128, 26, GOLD);
            DrawText("Press [1], [2], or [3] (or click a super-mod) to arm your defender",
                     screenW / 2 - 265, screenH / 2 - 90, 17, RAYWHITE);

            float cardW = 290.0f;
            float cardH = 132.0f;
            float gap = 20.0f;
            float totalW = cardW * 3.0f + gap * 2.0f;
            float startX = (screenW - totalW) * 0.5f;
            float cardY = screenH * 0.5f - cardH * 0.5f + 16.0f;
            Vector2 m = GetMousePosition();

            for (int s = 0; s < 3; ++s) {
                Rectangle r = {startX + s * (cardW + gap), cardY, cardW, cardH};
                bool hovered = CheckCollisionPointRec(m, r);
                DrawRectangleRec(r, hovered ? Color{48, 36, 68, 245} : Color{28, 24, 46, 240});
                DrawRectangleLinesEx(r, hovered ? 3.5f : 2.0f, superChoices[s].accent);

                int nextRank = build.GetSuper(superChoices[s].id) + 1;
                DrawText(TextFormat("[%d] %s (Mk.%d)", s + 1, superChoices[s].title, nextRank),
                         (int)r.x + 14, (int)r.y + 16, 17, superChoices[s].accent);
                DrawText(superChoices[s].statLine,
                         (int)r.x + 14, (int)r.y + 52, 14, RAYWHITE);
                DrawText("Also restores +25 HP & clears enemy fire",
                         (int)r.x + 14, (int)r.y + 92, 12, LIME);
            }
        } else if (choosingUpgrade && playerHp > 0) {
            DrawRectangle(0, 0, screenW, screenH, Fade(BLACK, 0.72f));
            DrawText(TextFormat("SECTOR WAVE %d CLEARED — INSTALL EXO-MOD", wave),
                     screenW / 2 - 285, screenH / 2 - 125, 26, GOLD);
            DrawText("Press [1], [2], or [3] (or click a module) to deploy next swarm wave",
                     screenW / 2 - 285, screenH / 2 - 88, 17, RAYWHITE);

            float cardW = 290.0f;
            float cardH = 126.0f;
            float gap = 20.0f;
            float totalW = cardW * 3.0f + gap * 2.0f;
            float startX = (screenW - totalW) * 0.5f;
            float cardY = screenH * 0.5f - cardH * 0.5f + 16.0f;
            Vector2 m = GetMousePosition();

            for (int s = 0; s < 3; ++s) {
                Rectangle r = {startX + s * (cardW + gap), cardY, cardW, cardH};
                bool hovered = CheckCollisionPointRec(m, r);
                DrawRectangleRec(r, hovered ? Color{36, 48, 72, 245} : Color{24, 30, 46, 240});
                DrawRectangleLinesEx(r, hovered ? 3.0f : 2.0f, draftChoices[s].accent);

                int nextStack = build.Get(draftChoices[s].id) + 1;
                DrawText(TextFormat("[%d] %s (Lv.%d)", s + 1, draftChoices[s].title, nextStack),
                         (int)r.x + 14, (int)r.y + 16, 17, draftChoices[s].accent);
                DrawText(draftChoices[s].statLine,
                         (int)r.x + 14, (int)r.y + 52, 14, RAYWHITE);
                DrawText("Also repairs +15 HP (+25 Max on Hull)",
                         (int)r.x + 14, (int)r.y + 88, 12, LIGHTGRAY);
            }
        }

        if (playerHp <= 0) {
            DrawRectangle(0, 0, screenW, screenH, Fade(BLACK, 0.78f));
            DrawText("DEFENSE BREACHED", screenW / 2 - 195, screenH / 2 - 115, 42, RED);
            DrawText(TextFormat("Wave Reached: %d   |   Best Wave: %d", wave, bestWave),
                     screenW / 2 - 195, screenH / 2 - 52, 22, RAYWHITE);
            DrawText(TextFormat("Final Score: %d   |   Best Score: %d   |   Counters: %d",
                                score, bestScore, countersLanded),
                     screenW / 2 - 275, screenH / 2 - 16, 20, GOLD);
            std::string endBuild = FormatBuildSummary(build);
            DrawText(endBuild.c_str(), screenW / 2 - (int)(endBuild.size() * 4.2f), screenH / 2 + 24, 16, SKYBLUE);
            DrawText("Press R to redeploy against The Abyssal Claw Invasion!",
                     screenW / 2 - 255, screenH / 2 + 68, 21, LIME);
        }

        EndDrawing();
    }

    audio.Shutdown();
    CloseWindow();
    return 0;
}