// (Part of PropActorsTest: the barrel.)
{
    C_ActorCreator *creator = g_actorCreators[ACTOR_BARREL];
    PR(r.barrel, creator && std::string(creator->GetName()) == "Barrel");
    // The barrel: its frame (a 0.6 m square box, 1 m high), the "body" and the liquid's surface "dekl".
    const float bx = rnd(-10.0f, 10.0f), bz = rnd(-10.0f, 10.0f);
    I3D_frame *frame = RbBoxFrame(nullptr, "barrel", -0.3f, 0.3f, 0.0f, 1.0f, -0.3f, 0.3f, S_vector(bx, 0.0f, bz));
    LinkUnder(frame, g_sector);
    frame->m_name = Stub(frame).name.c_str();
    I3D_frame *body = NewFrame(FRAME_DUMMY, "body", frame);
    body->m_name = Stub(body).name.c_str();
    body->m_localMatrix(3) = S_vector(0.0f, 0.05f, 0.0f);
    I3D_frame *dekl = NewFrame(FRAME_DUMMY, "dekl", frame);
    dekl->m_name = Stub(dekl).name.c_str();
    dekl->m_localMatrix(3) = S_vector(0.01f, 0.5f, -0.02f);
    static S_FrameData barrelData;
    static int barrelScript;
    barrelData = {};
    barrelData.script = reinterpret_cast<C_Script *>(&barrelScript);
    auto *barrel = static_cast<C_Barrel *>(creator->Create());
    PR(r.barrel, barrel->GetType() == ACTOR_BARREL && barrel->m_emptyLevel == 0.1f && barrel->m_fullLevel == 1.0f &&
                     barrel->m_leakRate == 0.1f && barrel->m_leakEffect == 90 && barrel->m_explosionEffect == 20 &&
                     barrel->m_hitPoints == 1000 && barrel->m_explosionLevel == 0.2f && !barrel->m_volumeOutWhenDestroyed &&
                     !barrel->m_exitHoles && std::string(barrel->m_surfaceName.c_str()) == "dekl" &&
                     std::string(barrel->m_bodyName.c_str()) == "body" && barrel->m_destroyedModel.IsEmpty() &&
                     barrel->m_holes.empty() && !barrel->m_sloshing);
    PR(r.barrel, barrel->SetFrame(frame) && barrel->GetFrame() == frame && !barrel->SetFrame(nullptr));
    barrelData.actor = barrel;
    frame->m_data = &barrelData;
    barrel->SetNetId(81);
    // The materials: the part's, else 11 (the frame's not asked).
    g_prMaterials.clear();
    g_prMaterials[frame] = 3;
    PR(r.barrel, barrel->GetMaterialId(body) == 11 && barrel->GetMaterialId(frame) == 3);
    g_prMaterials[body] = 8;
    PR(r.barrel, barrel->GetMaterialId(body) == 8);
    g_prMaterials.clear();

    // --- the properties: inside their first chunk (whatever its id); an unknown chunk is entered --------
    {
        const auto str = [](S_SaBytes &b, uint16_t id, const std::string &s) {
            b.Head(id, 6 + static_cast<uint32_t>(s.size()) + 1);
            b.Put(s.c_str(), s.size() + 1);
        };
        S_SaBytes inner;
        str(inner, 2000, "dekl");
        inner.Head(3000, 10).Val(0.9f).Head(4000, 10).Val(0.15f).Head(5000, 10).Val(0.25f).Head(6000, 10).Val(91);
        str(inner, 7000, "body");
        str(inner, 8000, "barrel_d.4ds");
        inner.Head(9000, 10).Val(500).Head(10000, 7).Val(uint8_t(1)).Head(11000, 10).Val(21).Head(12000, 10).Val(0.3f);
        inner.Head(13000, 7).Val(uint8_t(1));
        // (Unknown: entered; its own chunks read.)
        inner.Head(0x99, 6 + 10).Head(9000, 10).Val(450);
        S_SaBytes in;
        in.Head(0x777, 6 + 6 + static_cast<uint32_t>(inner.b.size())).Head(0x10, 6 + static_cast<uint32_t>(inner.b.size()));
        in.Put(inner.b.data(), inner.b.size());
        S_SaReader rd(in);
        PR(r.barrelProps, barrel->ReadProperties(rd.c));
        PR(r.barrelProps, barrel->m_fullLevel == 0.9f && barrel->m_emptyLevel == 0.15f && barrel->m_leakRate == 0.25f &&
                              barrel->m_leakEffect == 91 && std::string(barrel->m_destroyedModel.c_str()) == "barrel_d.4ds" &&
                              barrel->m_hitPoints == 450 && barrel->m_volumeOutWhenDestroyed && barrel->m_explosionEffect == 21 &&
                              barrel->m_explosionLevel == 0.3f && barrel->m_exitHoles);
        // Copied: everything but the frames and the state.
        auto *copy = static_cast<C_Barrel *>(creator->Create());
        barrel->m_level = 0.42f;
        copy->CopyFrom(barrel);
        PR(r.barrelProps, copy->m_level == 0.42f && copy->m_fullLevel == 0.9f && copy->m_emptyLevel == 0.15f &&
                              copy->m_leakRate == 0.25f && copy->m_leakEffect == 91 && copy->m_hitPoints == 450 &&
                              copy->m_explosionLevel == 0.3f && copy->m_explosionEffect == 21 && copy->m_exitHoles &&
                              copy->m_volumeOutWhenDestroyed && std::string(copy->m_destroyedModel.c_str()) == "barrel_d.4ds" &&
                              std::string(copy->m_bodyName.c_str()) == "body" && std::string(copy->m_surfaceName.c_str()) == "dekl");
        creator->Destroy(copy);
    }

    // --- the start: the box (the server), full, the parts' start placements, the hit points, the radius ----
    {
        g_gameRole = GAME_ROLE_LOCAL;
        body->m_rot = S_quat(0.6f, 0.8f, 0.0f, 0.0f);
        reset();
        barrel->StartMission(true);
        I3D_bbox hb;
        const I3D_bbox box = *I3DGetDriver()->GetHierarchyBBox(hb, frame);
        const float dx = box.min.x - box.max.x, dz = box.min.z - box.max.z;
        const float d = PrLength(dz * dz + dx * dx);
        const float radius = static_cast<float>(static_cast<double>(std::sqrt(d * d * 0.5f)) * 0.5);
        PR(r.barrelStart, g_activations == std::vector<bool>{true} && g_volumeLog.size() == 2 && g_volumeLog[0].op == 'A' &&
                              g_volumeLog[1].op == 'M' && g_volumeLog[0].volume == &barrel->m_volume &&
                              barrel->m_volume.type == COLLISION_TYPE_BARREL && barrel->m_volume.flags == COLLISION_SHAPE_BOX &&
                              barrel->m_volume.actor == barrel && RbSameBox(barrel->m_volume.bbox, box));
        PR(r.barrelStart, barrel->m_level == 0.9f && !barrel->m_empty && barrel->m_surface == dekl && barrel->m_body == body &&
                              SaSameQuat(barrel->m_bodyRot, body->m_rot) && RbSame(barrel->m_bodyPos, S_vector(0.0f, 0.05f, 0.0f)) &&
                              SaSameQuat(barrel->m_surfaceRot, dekl->m_rot) && barrel->m_health == 450.0f &&
                              !barrel->m_destroyed && barrel->m_radius == radius && std::fabs(radius - 0.3f) < 1e-6f &&
                              dekl->m_localMatrix.m[3][1] == 0.9f && dekl->m_localMatrix.m[3][0] == 0.01f && SaLogIs({"update dekl"}));
        // A client's start: no box.
        reset();
        barrel->StartMission(false);
        PR(r.barrelStart, g_volumeLog.empty() && g_activations == std::vector<bool>{false});
    }

    // --- the hits: through the body (bullets) a hole below the level (an exit hole on the far side), a
    // shot into the surface; sloshing --------------------------------------------------------------------------
    g_rbSplashStep = 1;
    g_rbSplashHandle = 500;
    {
        // (A server plays the hits; a game with the client's bit sends them, a local game's too.)
        g_gameRole = GAME_ROLE_SERVER;
        barrel->m_health = 1e6f;
        barrel->m_level = 0.9f;
        barrel->m_empty = false;
        barrel->m_sloshAmount = 0.0f;
        std::srand(11);
        reset();
        // A shot from +x at 0.4 m: in at +x, out at -x (the radius 0.3).
        barrel->Hit(HIT_BULLET, 10.0f, S_vector(bx + 0.3f, 0.4f, bz), S_vector(-3.0f, 0.0f, 0.0f), nullptr, 0);
        PR(r.barrelHit, barrel->m_holes.size() == 2 && g_rbSplashes.size() == 2 && g_rbSplashes[0].effect == 91 &&
                            RbSame(g_rbSplashes[0].pos, S_vector(bx + 0.3f, 0.4f, bz)) &&
                            RbSame(g_rbSplashes[0].dir, S_vector((bx + 0.3f) - bx, 0.0f, bz - bz)) &&
                            g_rbSplashes[0].parent == frame && g_rbSplashes[0].faceCamera && !g_rbSplashes[0].transient &&
                            std::fabs(g_rbSplashes[1].pos.x - (bx - 0.3f)) < 1e-5f && g_rbSplashes[1].pos.y == 0.4f &&
                            barrel->m_holes[0]->height == 0.4f && barrel->m_holes[1]->height == 0.4f &&
                            barrel->m_holes[0]->particle == 500 && barrel->m_holes[1]->particle == 501 &&
                            barrel->m_health == 1e6f - 10.0f);
        // (The slosh: its amount from RandFloat; the two extremes 1 degree either way of level.)
        std::srand(11);
        const float amount = 1.0f - RandFloat() * 0.2f;
        PR(r.barrelSlosh, barrel->m_sloshing && !barrel->m_sloshDamped && barrel->m_sloshPhase == 0.0f &&
                              barrel->m_sloshTime == 400 && barrel->m_sloshAmount == amount);
        // A second slosh adds to it (to 2.5 at most).
        std::srand(12);
        barrel->Hit(HIT_BULLET, 1.0f, S_vector(bx + 0.3f, 0.95f, bz), S_vector(-1.0f, 0.0f, 0.0f), nullptr, 0);
        std::srand(12);
        const float amount2 = RandFloat() * 0.2f + amount;
        PR(r.barrelSlosh, barrel->m_sloshAmount == amount2);
        barrel->m_sloshAmount = 2.5f;
        barrel->Hit(HIT_BULLET, 1.0f, S_vector(bx + 0.3f, 0.95f, bz), S_vector(-1.0f, 0.0f, 0.0f), nullptr, 0);
        PR(r.barrelSlosh, barrel->m_sloshAmount == 2.5f);
        barrel->m_sloshAmount = 0.1f;
        std::srand(13);
        barrel->Hit(HIT_BULLET, 1.0f, S_vector(bx + 0.3f, 0.95f, bz), S_vector(-1.0f, 0.0f, 0.0f), nullptr, 0);
        std::srand(13);
        PR(r.barrelSlosh, barrel->m_sloshAmount == RandFloat() * 0.2f + 0.1f);
        // Holes sorted by height; above the level or under the empty level: none.
        reset();
        barrel->Hit(HIT_BULLET, 1.0f, S_vector(bx + 0.3f, 0.2f, bz), S_vector(-1.0f, 0.0f, 0.0f), nullptr, 0);
        PR(r.barrelHit, barrel->m_holes.size() == 4 && barrel->m_holes[0]->height == 0.2f && barrel->m_holes[1]->height == 0.2f &&
                            barrel->m_holes[2]->height == 0.4f && barrel->m_holes[0]->particle == 502 &&
                            barrel->m_holes[1]->particle == 503);
        reset();
        barrel->Hit(HIT_BULLET, 1.0f, S_vector(bx + 0.3f, 0.9f, bz), S_vector(-1.0f, 0.0f, 0.0f), nullptr, 0);
        barrel->Hit(HIT_BULLET, 1.0f, S_vector(bx + 0.3f, 0.15f, bz), S_vector(-1.0f, 0.0f, 0.0f), nullptr, 0);
        PR(r.barrelHit, barrel->m_holes.size() == 4 && g_rbSplashes.empty());
        // Without exit holes: only the entry.
        barrel->m_exitHoles = false;
        reset();
        barrel->Hit(HIT_BULLET, 1.0f, S_vector(bx + 0.3f, 0.6f, bz), S_vector(-1.0f, 0.0f, 0.0f), nullptr, 0);
        PR(r.barrelHit, barrel->m_holes.size() == 5 && barrel->m_holes[4]->height == 0.6f && g_rbSplashes.size() == 1);
        barrel->m_exitHoles = true;
        // A shot at an angle: the exit where the line leaves the cylinder (the oracle from the asm).
        {
            const S_vector pos(bx + 0.2f, 0.5f, bz + 0.1f), dir(-0.8f, -0.3f, -0.5f);
            S_matrix inv;
            inv.Inverse(frame->GetMatrix());
            const S_vector local = pos * inv;
            S_vector dn = dir;
            PrNormalize(dn, (dn.y * dn.y + dn.z * dn.z) + dn.x * dn.x);
            const S_vector &at = frame->GetWorldPos();
            S_vector through = S_vector(at.x + dn.x, dn.y + at.y, dn.z + at.z) * inv;
            PrNormalize(through, (through.y * through.y + through.z * through.z) + through.x * through.x);
            const float ox = -local.x, oz = -local.z;
            const float k = 1.0f / PrLength(through.z * through.z + through.x * through.x);
            const float t = ((through.z * k) * oz + ox * (through.x * k)) + (k * 0.0f) * 0.0f;
            const float h = std::sqrt(barrel->m_radius * barrel->m_radius - ((oz * oz + ox * ox) - t * t));
            const float ta = (t + h) * k, tb = (t - h) * k;
            const S_vector a(ta * through.x + local.x, ta * through.y + local.y, ta * through.z + local.z);
            const S_vector b(tb * through.x + local.x, tb * through.y + local.y, tb * through.z + local.z);
            const float da = ((local.x - a.x) * (local.x - a.x) + (local.y - a.y) * (local.y - a.y)) + (local.z - a.z) * (local.z - a.z);
            const float db = ((local.x - b.x) * (local.x - b.x) + (local.y - b.y) * (local.y - b.y)) + (local.z - b.z) * (local.z - b.z);
            const S_vector exit = da <= db ? b : a;
            const S_vector exitWorld = exit * frame->GetMatrix();
            reset();
            barrel->Hit(HIT_BULLET, 1.0f, pos, dir, nullptr, 0);
            PR(r.barrelHit, g_rbSplashes.size() == 2 && RbSame(g_rbSplashes[1].pos, exitWorld) &&
                                RbSame(g_rbSplashes[1].dir, S_vector(exitWorld.x - at.x, 0.0f, exitWorld.z - at.z)) &&
                                barrel->m_holes.size() == 7);
            // Into the surface: the slosh first; the holes where the line leaves: the first one between the
            // levels (a), else the other.
            barrel->m_sloshing = false;
            S_vector sn = dir;
            PrNormalize(sn, (sn.x * sn.x + sn.y * sn.y) + sn.z * sn.z);
            S_vector sd = sn;
            PrNormalize(sd, (sd.y * sd.y + sd.z * sd.z) + sd.x * sd.x);
            S_vector sthrough = S_vector(at.x + sd.x, sd.y + at.y, sd.z + at.z) * inv;
            PrNormalize(sthrough, (sthrough.y * sthrough.y + sthrough.z * sthrough.z) + sthrough.x * sthrough.x);
            const float sk = 1.0f / PrLength(sthrough.z * sthrough.z + sthrough.x * sthrough.x);
            const float st = ((sthrough.z * sk) * oz + ox * (sthrough.x * sk)) + (sk * 0.0f) * 0.0f;
            const float sh = std::sqrt(barrel->m_radius * barrel->m_radius - ((oz * oz + ox * ox) - st * st));
            const float sta = (st + sh) * sk;
            const S_vector sa(sta * sthrough.x + local.x, sta * sthrough.y + local.y, sta * sthrough.z + local.z);
            reset();
            barrel->Hit(HIT_EXPLOSION, 1.0f, pos, dir, nullptr, static_cast<int>(reinterpret_cast<intptr_t>(dekl)));
            const bool aLeaks = sa.y < barrel->m_level && sa.y > barrel->m_emptyLevel;
            PR(r.barrelHit, barrel->m_sloshing && g_rbSplashes.size() == 1 && barrel->m_holes.size() == 8 &&
                                (!aLeaks || RbSame(g_rbSplashes[0].pos, sa * frame->GetMatrix())));
        }
        // Explosions into the body: only the hit points; other kinds of hit nothing more.
        reset();
        barrel->Hit(HIT_EXPLOSION, 1.0f, S_vector(bx + 0.3f, 0.5f, bz), S_vector(-1.0f, 0.0f, 0.0f), nullptr, 0);
        barrel->Hit(HIT_MELEE, 1.0f, S_vector(bx + 0.3f, 0.5f, bz), S_vector(-1.0f, 0.0f, 0.0f), nullptr, 0);
        PR(r.barrelHit, g_rbSplashes.empty() && barrel->m_holes.size() == 8);
        // An empty barrel: nothing.
        barrel->m_empty = true;
        reset();
        barrel->Hit(HIT_BULLET, 1.0f, S_vector(bx + 0.3f, 0.5f, bz), S_vector(-1.0f, 0.0f, 0.0f), nullptr, 0);
        PR(r.barrelHit, g_rbSplashes.empty() && barrel->m_holes.size() == 8);
        barrel->m_empty = false;
    }

    // --- the leak: holes over the level less 0.09 stop; each other one lowers it ----------------------------
    {
        barrel->m_sloshing = false;
        barrel->m_bodyTipping = false;
        float level = barrel->m_level;
        for (int step = 0; step < 400 && !barrel->m_empty && !barrel->m_holes.empty(); ++step) {
            const int t = static_cast<int>(rnd(20.0f, 200.0f));
            std::vector<std::string> log;
            size_t holes = barrel->m_holes.size();
            for (size_t i = holes; i-- > 0;)
                if (level - 0.09f < barrel->m_holes[i]->height) {
                    log.push_back("finish " + std::to_string(barrel->m_holes[i]->particle));
                    --holes;
                }
            reset();
            barrel->Update(t);
            level = level - static_cast<float>(static_cast<uint32_t>(holes)) * 0.25f * static_cast<float>(t) * 0.001f;
            bool empty = false;
            if (level <= 0.15f) {
                empty = true;
                level = 0.15f;
            }
            log.push_back("update dekl");
            PR(r.barrelLeak, barrel->m_level == level && barrel->m_holes.size() == holes && barrel->m_empty == empty &&
                                 dekl->m_localMatrix.m[3][1] == level && SaLogIs(log));
        }
        // (Every hole stops on the way down: the level stays over the empty level.)
        PR(r.barrelLeak, barrel->m_holes.empty() && !barrel->m_empty && barrel->m_level > 0.15f);
        // Emptied by a long step: the level at the empty level; the hole left stops on the next.
        barrel->m_level = 0.35f;
        barrel->m_holes.push_back(new C_Barrel::S_Hole{0.2f, 999});
        reset();
        barrel->Update(1000);
        PR(r.barrelLeak, barrel->m_empty && barrel->m_level == 0.15f && barrel->m_holes.size() == 1 &&
                             dekl->m_localMatrix.m[3][1] == 0.15f && SaLogIs({"update dekl"}));
        reset();
        barrel->Update(50);
        PR(r.barrelLeak, barrel->m_holes.empty() && barrel->m_level == 0.15f && SaLogIs({"finish 999", "update dekl"}));
        // Empty without holes: nothing.
        reset();
        barrel->Update(50);
        PR(r.barrelLeak, g_itLog.empty());
        // Tick: on a pure client nothing.
        barrel->m_empty = false;
        barrel->m_level = 0.6f;
        g_gameRole = GAME_ROLE_CLIENT;
        reset();
        barrel->Tick(40);
        PR(r.barrelLeak, g_itLog.empty());
        g_gameRole = GAME_ROLE_SERVER;
        reset();
        barrel->Tick(40);
        PR(r.barrelLeak, SaLogIs({"update dekl"}));
        g_gameRole = GAME_ROLE_LOCAL;
    }

    // --- the slosh and the body's tip (a bump): the oracle from the asm --------------------------------------
    {
        I3D_frame *otherFrame = NewFrame(FRAME_DUMMY, "bumper", nullptr);
        otherFrame->m_localMatrix(3) = S_vector(bx + 2.0f, 0.3f, bz + 0.5f);
        auto *other = new C_Human();
        other->m_frame = otherFrame;
        g_gameRole = GAME_ROLE_SERVER;
        barrel->m_empty = true;                         // (no leak in the way)
        barrel->m_sloshAmount = 0.0f;
        std::srand(21);
        reset();
        PR(r.barrelSlosh, barrel->OnCollisionEx(other, nullptr, 0, 0, 0, 0, 0) && barrel->m_bodyTipping &&
                              !barrel->m_bodyTippingBack && barrel->m_bodyTipTime == 400 && barrel->m_sloshing);
        // (Tipping: no second bump.)
        PR(r.barrelSlosh, !barrel->OnCollisionEx(other, nullptr, 0, 0, 0, 0, 0));
        // The body's tip and the slosh's sway, step by step.
        const S_quat bodyRot = barrel->m_bodyRot, tipRot = barrel->m_bodyTipRot;
        const S_quat from = barrel->m_sloshFrom, to = barrel->m_sloshTo;
        int tipTime = 400;
        bool tipping = true, back = false;
        int sloshTime = 400;
        float amount = barrel->m_sloshAmount, phase = 0.0f;
        bool sloshing = true, damped = false;
        for (int step = 0; step < 400 && (tipping || sloshing); ++step) {
            const int t = static_cast<int>(rnd(10.0f, 90.0f));
            reset();
            barrel->Update(t);
            if (tipping) {
                tipTime -= t;
                float f;
                if (!back) {
                    if (tipTime < 0) {
                        tipTime = 400;
                        back = true;
                        f = 1.0f;
                    } else {
                        f = 1.0f - static_cast<float>(tipTime) * 0.0025f;
                    }
                } else {
                    if (tipTime < 0) {
                        tipTime = 0;
                        tipping = false;
                    }
                    f = static_cast<float>(tipTime) * 0.0025f;
                }
                PR(r.barrelSlosh, SaSameQuat(body->m_rot, SaNormalized(bodyRot.Slerp(tipRot, f, false))) &&
                                      barrel->m_bodyTipping == tipping && barrel->m_bodyTipTime == tipTime);
            }
            if (sloshing) {
                sloshTime -= t;
                if (!damped)
                    amount = amount - static_cast<float>(t) * 0.0001f;
                if (amount <= 0.0f) {
                    damped = true;
                    amount = 0.0001f;
                }
                float c = std::cos(static_cast<float>(sloshTime) * 0.00392699009f);
                if (damped && ((phase <= 0.5f && c >= 0.5f) || (phase >= 0.5f && c <= 0.5f))) {
                    c = 0.0f;
                    sloshing = false;
                }
                phase = c;
                const S_quat sway = from.Slerp(to, c * amount, false);
                const S_matrix turn = RotateYMatrix(1.0f * static_cast<float>(sloshTime - 400) * 0.00012f);
                PR(r.barrelSlosh, SaSameQuat(dekl->m_rot, SaNormalized(sway.RotateByMatrix(turn))) &&
                                      barrel->m_sloshing == sloshing && barrel->m_sloshPhase == phase &&
                                      barrel->m_sloshAmount == amount && barrel->m_sloshDamped == damped);
            }
        }
        PR(r.barrelSlosh, !barrel->m_bodyTipping && !barrel->m_sloshing);
        // (Barrels of even ids turn the other way.)
        barrel->SetNetId(82);
        barrel->m_sloshing = true;
        barrel->m_sloshDamped = false;
        barrel->m_sloshAmount = 1.0f;
        barrel->m_sloshTime = 300;
        barrel->Update(20);
        const S_quat sway = barrel->m_sloshFrom.Slerp(barrel->m_sloshTo, barrel->m_sloshPhase * barrel->m_sloshAmount, false);
        PR(r.barrelSlosh, SaSameQuat(dekl->m_rot,
                                     SaNormalized(sway.RotateByMatrix(RotateYMatrix(-1.0f * static_cast<float>(280 - 400) * 0.00012f)))));
        barrel->m_sloshing = false;
        barrel->SetNetId(81);
        // Without a body: no bump.
        barrel->m_body = nullptr;
        PR(r.barrelSlosh, !barrel->OnCollisionEx(other, nullptr, 0, 0, 0, 0, 0));
        barrel->m_body = body;
        barrel->m_empty = false;
        delete other;
    }

    // --- destroyed: when the hit points run out while the level is over the explosion level ----------------
    {
        g_gameRole = GAME_ROLE_SERVER;
        barrel->m_level = 0.25f;                        // (under 0.3: not destroyed)
        barrel->m_health = 5.0f;
        reset();
        barrel->Hit(HIT_BULLET, 10.0f, S_vector(bx + 0.3f, 0.2f, bz), S_vector(-1.0f, 0.0f, 0.0f), nullptr, 0);
        PR(r.barrelDestroy, barrel->m_health == 0.0f && !barrel->m_empty && !barrel->m_holes.empty());
        // (No hit points left: no more damage either.)
        barrel->Hit(HIT_BULLET, 10.0f, S_vector(bx + 0.3f, 0.2f, bz), S_vector(-1.0f, 0.0f, 0.0f), nullptr, 0);
        PR(r.barrelDestroy, barrel->m_health == 0.0f);
        const std::vector<int> particles = [&] {
            std::vector<int> p;
            for (size_t i = barrel->m_holes.size(); i-- > 0;)
                p.push_back(barrel->m_holes[i]->particle);
            return p;
        }();
        barrel->m_level = 0.5f;
        barrel->m_health = 5.0f;
        barrel->m_bodyTipping = true;
        static bool wreckFilled;
        wreckFilled = false;
        struct S_Wreck {
            static void Fill(I3D_model *object)
            {
                g_itNames[reinterpret_cast<I3D_frame *>(object)] = "wreck";
                wreckFilled = true;
            }
        };
        g_cacheFill = &S_Wreck::Fill;
        g_cacheLoads.clear();
        const int deaths = g_deaths;
        frame->m_localMatrix(3) = S_vector(bx, 0.0f, bz);
        frame->m_rot = S_quat(0.8f, 0.0f, 0.6f, 0.0f);
        reset();
        barrel->Hit(HIT_BULLET, 10.0f, S_vector(bx + 0.3f, 0.4f, bz), S_vector(-1.0f, 0.0f, 0.0f), nullptr, 0);
        PR(r.barrelDestroy, barrel->m_health == 0.0f && barrel->m_empty && barrel->m_holes.empty() && !barrel->m_bodyTipping &&
                                g_particlesDestroyed == particles && g_cacheLoads == std::vector<std::string>{"barrel_d"} &&
                                wreckFilled && barrel->m_destroyed && barrel->m_destroyed->m_parent == g_sector &&
                                RbSame(barrel->m_destroyed->m_localMatrix(3), S_vector(bx, 0.0f, bz)) &&
                                SaSameQuat(barrel->m_destroyed->m_rot, SaNormalized(frame->m_rot)) && !frame->IsOn() &&
                                g_volumeLog.size() == 1 && g_volumeLog[0].op == 'R' && g_deaths == deaths + 1 &&
                                g_rbSplashes.empty());
        PR(r.barrelDestroy, SaLogIs({"createframe 9", "link wreck sector 0", "on wreck 1", "setname wreck barrel_destroyed",
                                     "update barrel_destroyed", "on barrel 0", "update barrel"}));
        // A local game: the message; played (the explosion, the script).
        barrel->StopMission(true);
        barrel->StartMission(true);
        g_gameRole = GAME_ROLE_LOCAL;
        barrel->m_health = 1.0f;
        barrel->m_level = 0.5f;
        reset();
        barrel->Hit(HIT_EXPLOSION, 2.0f, S_vector(bx, 0.5f, bz), S_vector(0, -1, 0), nullptr, 0);
        PR(r.barrelDestroy, !barrel->m_empty && PrBits(0) == std::vector<std::pair<uint32_t, int>>({{2, 3}}));
        g_readBits = {{2, 3}};
        g_readPos = 0;
        reset();
        barrel->OnMessage(reinterpret_cast<C_BitStream *>(&g_readBits));
        PR(r.barrelDestroy, barrel->m_empty && g_volumeLog.size() == 1 && g_deaths == deaths + 2 && g_rbSplashes.size() == 1 &&
                                g_rbSplashes[0].effect == 21 && RbSame(g_rbSplashes[0].pos, frame->GetWorldPos()) &&
                                RbSame(g_rbSplashes[0].dir, S_vector(0, 1, 0)) && !g_rbSplashes[0].parent &&
                                g_rbSplashes[0].faceCamera && !g_rbSplashes[0].transient);
        // A pure client: no script, no box out.
        barrel->StopMission(true);
        barrel->StartMission(true);
        g_gameRole = GAME_ROLE_CLIENT;
        g_readPos = 0;
        reset();
        barrel->OnMessage(reinterpret_cast<C_BitStream *>(&g_readBits));
        PR(r.barrelDestroy, barrel->m_empty && g_volumeLog.empty() && g_deaths == deaths + 2 && g_rbSplashes.size() == 1);
        g_gameRole = GAME_ROLE_SERVER;
        // The model missing: none.
        barrel->StopMission(true);
        barrel->StartMission(true);
        g_cacheFailName = "barrel_d";
        barrel->m_health = 1.0f;
        barrel->m_level = 0.5f;
        barrel->m_volumeOutWhenDestroyed = false;
        reset();
        barrel->Hit(HIT_BULLET, 2.0f, S_vector(bx + 0.3f, 0.5f, bz), S_vector(-1.0f, 0.0f, 0.0f), nullptr, 0);
        PR(r.barrelDestroy, barrel->m_empty && !barrel->m_destroyed && g_volumeLog.empty() &&
                                std::count(g_itLog.begin(), g_itLog.end(), "release new") == 1);
        g_cacheFailName.clear();
        barrel->m_volumeOutWhenDestroyed = true;
        // Without a model file: nothing loaded.
        barrel->StopMission(true);
        barrel->StartMission(true);
        barrel->m_destroyedModel = "";
        barrel->m_health = 1.0f;
        barrel->m_level = 0.5f;
        g_cacheLoads.clear();
        reset();
        barrel->Hit(HIT_BULLET, 2.0f, S_vector(bx + 0.3f, 0.5f, bz), S_vector(-1.0f, 0.0f, 0.0f), nullptr, 0);
        PR(r.barrelDestroy, barrel->m_empty && !barrel->m_destroyed && g_cacheLoads.empty());
        barrel->m_destroyedModel = "barrel_d.4ds";
        barrel->StopMission(true);
        barrel->StartMission(true);
    }

    // --- the network: a client sends what hit it (the server plays it) ---------------------------------------
    {
        const auto bits = [](uint32_t kind, std::initializer_list<S_vector> vs, int extra = -1) {
            std::vector<std::pair<uint32_t, int>> b = {{kind, 3}};
            if (extra >= 0)
                b.emplace_back(uint32_t(extra), 1);
            for (const S_vector &v : vs)
                for (float f : {v.x, v.y, v.z})
                    b.emplace_back(PrFloatBits(f), 32);
            return b;
        };
        g_gameRole = GAME_ROLE_CLIENT;
        barrel->m_health = 100.0f;
        barrel->m_level = 0.5f;
        reset();
        const S_vector pos(bx + 0.3f, 0.3f, bz), dir(-2.0f, 0.0f, 0.0f);
        barrel->Hit(HIT_BULLET, 1.0f, pos, dir, nullptr, 0);
        PR(r.barrelNet, g_streams.size() == 1 && g_streams[0]->id == 81 && g_streams[0]->sent && PrBits(0) == bits(0, {pos, dir}) &&
                            barrel->m_holes.empty());
        reset();
        barrel->Hit(HIT_BULLET, 1.0f, pos, dir, nullptr, static_cast<int>(reinterpret_cast<intptr_t>(dekl)));
        PR(r.barrelNet, PrBits(0) == bits(3, {pos, S_vector(-1.0f, 0.0f, 0.0f)}) && !barrel->m_sloshing);
        barrel->m_health = 1.0f;
        reset();
        barrel->Hit(HIT_BULLET, 2.0f, pos, dir, nullptr, 0);
        PR(r.barrelNet, PrBits(0) == std::vector<std::pair<uint32_t, int>>({{2, 3}}) && !barrel->m_empty && g_streams.size() == 1);
        // A bump: the body's and the surface's points.
        I3D_frame *otherFrame = NewFrame(FRAME_DUMMY, "bumper2", nullptr);
        otherFrame->m_localMatrix(3) = S_vector(bx - 1.0f, 0.0f, bz);
        auto *other = new C_Human();
        other->m_frame = otherFrame;
        reset();
        PR(r.barrelNet, barrel->OnCollisionEx(other, nullptr, 0, 0, 0, 0, 0) && !barrel->m_bodyTipping &&
                            PrBits(0).size() == 1 + 1 + 6 && PrBits(0)[0] == std::make_pair(1u, 3) &&
                            PrBits(0)[1] == std::make_pair(1u, 1));
        delete other;
        // The server: the messages played.
        g_gameRole = GAME_ROLE_LOCAL;
        barrel->m_health = 100.0f;
        g_readBits = bits(0, {pos, dir});
        g_readPos = 0;
        reset();
        barrel->OnMessage(reinterpret_cast<C_BitStream *>(&g_readBits));
        PR(r.barrelNet, barrel->m_holes.size() == 2 && barrel->m_holes[0]->height == 0.3f && barrel->m_sloshing);
        barrel->m_sloshing = false;
        g_readBits = bits(1, {S_vector(0.2f, 0.4f, 0.1f), S_vector(0.0f, 0.0f, 1.0f)}, 1);
        g_readPos = 0;
        barrel->OnMessage(reinterpret_cast<C_BitStream *>(&g_readBits));
        PR(r.barrelNet, barrel->m_bodyTipping && barrel->m_sloshing && !g_readMismatch);
        barrel->m_bodyTipping = false;
        barrel->m_sloshing = false;
        g_readBits = bits(1, {S_vector(0.0f, 0.0f, 1.0f)}, 0);
        g_readPos = 0;
        barrel->OnMessage(reinterpret_cast<C_BitStream *>(&g_readBits));
        PR(r.barrelNet, !barrel->m_bodyTipping && barrel->m_sloshing);
        g_readBits = bits(3, {pos, S_vector(-1.0f, 0.0f, 0.0f)});
        g_readPos = 0;
        reset();
        barrel->OnMessage(reinterpret_cast<C_BitStream *>(&g_readBits));
        PR(r.barrelNet, g_rbSplashes.size() == 1);
        g_readBits = {{2, 3}};
        g_readPos = 0;
        barrel->OnMessage(reinterpret_cast<C_BitStream *>(&g_readBits));
        PR(r.barrelNet, barrel->m_empty && barrel->m_destroyed);
        g_readBits = {{5, 3}};
        g_readPos = 0;
        barrel->OnMessage(reinterpret_cast<C_BitStream *>(&g_readBits));
        barrel->StopMission(true);
        barrel->StartMission(true);
    }

    // --- the saved game: chunk 1 holding the base's, 1000 empty, 2000 the level (the lowest hole's while it
    // leaks), 3000 the hit points; read back (destroyed at none) ----------------------------------------------
    {
        g_gameRole = GAME_ROLE_SERVER;
        barrel->m_empty = false;
        barrel->m_level = 0.7f;
        barrel->m_health = 123.75f;
        bool saved = false;
        S_SaBytes e;
        e.Head(0x777, 6 + 6 + 10 + 7 + 10 + 10).Head(1, 6 + 10 + 7 + 10 + 10).Head(0x3039, 10).Val(g_actorSaveTag);
        e.Head(1000, 7).Val(uint8_t(0)).Head(2000, 10).Val(0.7f).Head(3000, 10).Val(123);
        PR(r.barrelSave, SaWritten([&](C_chunk &c) { return barrel->SaveGame(c); }, &saved) == e.b && saved);
        // Leaking: the lowest hole's height; no hit points at all: no 3000.
        barrel->Hit(HIT_BULLET, 0.0f, S_vector(bx + 0.3f, 0.35f, bz), S_vector(-1.0f, 0.0f, 0.0f), nullptr, 0);
        barrel->m_hitPoints = 0;
        S_SaBytes e2;
        e2.Head(0x777, 6 + 6 + 10 + 7 + 10).Head(1, 6 + 10 + 7 + 10).Head(0x3039, 10).Val(g_actorSaveTag);
        e2.Head(1000, 7).Val(uint8_t(0)).Head(2000, 10).Val(0.35f);
        PR(r.barrelSave, SaWritten([&](C_chunk &c) { return barrel->SaveGame(c); }, &saved) == e2.b && saved);
        barrel->m_hitPoints = 450;
        // Read back (another chunk skipped).
        S_SaBytes in;
        in.Head(0x777, 6 + 6 + 10 + 7 + 7 + 10 + 10).Head(1, 6 + 10 + 7 + 7 + 10 + 10).Head(0x3039, 10).Val(g_actorSaveTag);
        in.Head(0x50, 7).Val(uint8_t(9)).Head(1000, 7).Val(uint8_t(1)).Head(2000, 10).Val(0.45f).Head(3000, 10).Val(77);
        S_SaReader rd(in);
        reset();
        PR(r.barrelSave, barrel->LoadGame(rd.c) && barrel->m_empty && barrel->m_level == 0.45f && barrel->m_health == 77.0f &&
                             dekl->m_localMatrix.m[3][1] == 0.45f);
        // No hit points left: destroyed (a saved game: no explosion).
        S_SaBytes dead;
        dead.Head(0x777, 6 + 6 + 10 + 10).Head(1, 6 + 10 + 10).Head(0x3039, 10).Val(g_actorSaveTag).Head(3000, 10).Val(0);
        S_SaReader rd2(dead);
        const int deaths = g_deaths;
        reset();
        PR(r.barrelSave, barrel->LoadGame(rd2.c) && barrel->m_empty && g_deaths == deaths && g_rbSplashes.empty() &&
                             barrel->m_destroyed);
        // Another chunk at the top: nothing read.
        S_SaBytes other;
        other.Head(0x777, 6 + 6 + 10).Head(2, 6 + 10).Head(1000, 10).Val(0);
        S_SaReader rd3(other);
        barrel->m_level = 0.5f;
        PR(r.barrelSave, barrel->LoadGame(rd3.c) && barrel->m_level == 0.5f);
    }

    // --- stop and restart: everything back ------------------------------------------------------------------
    {
        frame->SetOn(true);
        body->m_rot = S_quat(0.0f, 1.0f, 0.0f, 0.0f);
        body->m_localMatrix(3) = S_vector(1, 2, 3);
        dekl->m_rot = S_quat(0.0f, 0.0f, 1.0f, 0.0f);
        reset();
        barrel->StopMission(true);
        PR(r.barrelStop, !barrel->m_destroyed && !barrel->m_body && !barrel->m_surface && barrel->m_holes.empty() &&
                             frame->IsOn() && g_deactivations == std::vector<bool>{true} &&
                             SaSameQuat(body->m_rot, SaNormalized(barrel->m_bodyRot)) && RbSame(body->m_localMatrix(3), barrel->m_bodyPos) &&
                             SaSameQuat(dekl->m_rot, SaNormalized(barrel->m_surfaceRot)) &&
                             std::count(g_itLog.begin(), g_itLog.end(), "release barrel_destroyed") == 1 &&
                             std::count(g_itLog.begin(), g_itLog.end(), "link barrel_destroyed 0 0") == 1);
        // (The box out only for a server's stop of a shown frame.)
        PR(r.barrelStop, g_volumeLog.size() == 1 && g_volumeLog[0].op == 'R');
        barrel->StartMission(true);
        reset();
        barrel->RestartMission(true);
        PR(r.barrelStop, g_volumeLog.empty() && barrel->m_body == body && barrel->m_level == barrel->m_fullLevel &&
                             g_deactivations == std::vector<bool>{false} && g_activations == std::vector<bool>{false});
        frame->SetOn(false);
        reset();
        barrel->StopMission(true);
        PR(r.barrelStop, g_volumeLog.empty() && frame->IsOn());
    }

    // --- the rest: a new barrel, the copy's names, the start's fields, the stop's calls, the bump's tip and
    // extremes (from the asm), the edges of the damage, the leak and the sway, the exit's other side, the
    // bump's points normalised as the inline copies do, the chunks after others ------------------------------
    {
        auto *fresh = static_cast<C_Barrel *>(creator->Create());
        PR(r.barrel, fresh->m_level == 0.0f && fresh->m_sloshAmount == 0.0f && fresh->m_sloshPhase == 0.0f &&
                         fresh->m_sloshTime == 0 && fresh->m_bodyPos.x == 0.0f && fresh->m_bodyPos.y == 0.0f &&
                         fresh->m_bodyPos.z == 0.0f && fresh->m_bodyTipTime == 0 && fresh->m_health == 0.0f &&
                         fresh->m_radius == 0.0f);
        barrel->m_surfaceName = "dekl2";
        barrel->m_bodyName = "body2";
        fresh->CopyFrom(barrel);
        PR(r.barrelProps, std::string(fresh->m_surfaceName.c_str()) == "dekl2" && std::string(fresh->m_bodyName.c_str()) == "body2");
        creator->Destroy(fresh);
        barrel->m_surfaceName = "dekl";
        barrel->m_bodyName = "body";
        // (The properties inside a first chunk of a property's id: still read.)
        {
            S_SaBytes inner;
            inner.Head(9000, 10).Val(321).Head(12000, 10).Val(0.3f);
            S_SaBytes in;
            in.Head(0x777, 6 + 6 + static_cast<uint32_t>(inner.b.size())).Head(3000, 6 + static_cast<uint32_t>(inner.b.size()));
            in.Put(inner.b.data(), inner.b.size());
            S_SaReader rd(in);
            barrel->ReadProperties(rd.c);
            PR(r.barrelProps, barrel->m_hitPoints == 321);
            barrel->m_hitPoints = 450;
        }

        // The start: no tip, no wreck (whatever was left).
        frame->m_rot = S_quat(1.0f, 0.0f, 0.0f, 0.0f);
        frame->m_frameFlags = (frame->m_frameFlags & ~(FRMFLAGS_WMATRIX_VALID | FRMFLAGS_LMATRIX_VALID)) | FRMFLAGS_ROT_DIRTY;
        g_gameRole = GAME_ROLE_SERVER;
        barrel->m_bodyTipTime = 77;
        barrel->m_bodyTipping = true;
        static int stale;
        barrel->m_destroyed = reinterpret_cast<I3D_model *>(&stale);
        barrel->StartMission(true);
        PR(r.barrelStart, barrel->m_bodyTipTime == 0 && !barrel->m_bodyTipping && !barrel->m_destroyed);

        // The bump's tip: the body's rotation turned 5 degrees about the hit's point crossed with y (the zero
        // terms kept); the sway's extremes: the surface's rotation levelled, tilted 1 degree either way about
        // the direction crossed with y.
        dekl->m_rot = SaNormalized(S_quat(0.9f, 0.2f, 0.3f, 0.1f));
        dekl->m_frameFlags = (dekl->m_frameFlags & ~(FRMFLAGS_WMATRIX_VALID | FRMFLAGS_LMATRIX_VALID)) | FRMFLAGS_ROT_DIRTY;
        barrel->m_bodyRot = SaNormalized(S_quat(0.8f, 0.1f, 0.5f, 0.2f));
        barrel->m_bodyTippingBack = true;
        barrel->m_bodyTipRot = S_quat(0.0f, 1.0f, 0.0f, 0.0f);
        barrel->m_sloshPhase = 0.7f;
        barrel->m_sloshDamped = true;
        barrel->m_sloshAmount = 0.0f;
        const S_vector tipAt(0.3f, 0.4f, 0.2f), sloshDir(0.5f, -0.6f, 0.62f);
        std::srand(31);
        g_prSetDirs.clear();
        barrel->Slosh(tipAt, sloshDir, true);
        const std::vector<std::pair<S_vector, float>> setDirs = g_prSetDirs;
        {
            S_quat tip;
            tip.Make(S_vector(tipAt.y * 0.0f - tipAt.z, tipAt.z * 0.0f - tipAt.x * 0.0f, tipAt.x - tipAt.y * 0.0f), 0.0872664601f);
            const S_quat tipRot = barrel->m_bodyRot.RotateByMatrix(tip.RotationMatrix());
            const S_vector axis(sloshDir.y * 0.0f - sloshDir.z, sloshDir.z * 0.0f - sloshDir.x * 0.0f, sloshDir.x - sloshDir.y * 0.0f);
            S_quat from = dekl->GetRot();
            S_vector level = from.GetDir();
            level.y = 0.0f;
            PrNormalize(level, level.z * level.z + level.x * level.x);
            from.SetDir(level, 0.0f);
            S_quat to = dekl->GetRot();
            level = to.GetDir();
            level.y = 0.0f;
            PrNormalize(level, level.z * level.z + level.x * level.x);
            to.SetDir(level, 0.0f);
            const S_vector levelTo = level;
            // (Each extreme turned to its level direction, no roll.)
            PR(r.barrelSlosh, setDirs.size() == 2 && RbSame(setDirs[0].first, levelTo) && setDirs[0].second == 0.0f &&
                                  RbSame(setDirs[1].first, levelTo) && setDirs[1].second == 0.0f &&
                                  std::fabs(levelTo.x * levelTo.x + levelTo.z * levelTo.z - 1.0f) < 1e-5f);
            S_quat tilt;
            tilt.Make(axis, 0.0174532924f);
            from = from.RotateByMatrix(tilt.RotationMatrix());
            tilt.Make(axis, -0.0174532924f);
            to = to.RotateByMatrix(tilt.RotationMatrix());
            PR(r.barrelSlosh, barrel->m_bodyTipping && !barrel->m_bodyTippingBack && SaSameQuat(barrel->m_bodyTipRot, tipRot) &&
                                  SaSameQuat(barrel->m_sloshFrom, from) && SaSameQuat(barrel->m_sloshTo, to) &&
                                  barrel->m_sloshPhase == 0.0f && !barrel->m_sloshDamped);
        }
        // (Run out exactly: still tipping over; then back.)
        barrel->m_sloshing = false;
        barrel->m_empty = true;
        barrel->m_bodyTipTime = 10;
        g_lastSlerpFlag = true;
        reset();
        barrel->Update(10);
        PR(r.barrelSlosh, !g_lastSlerpFlag);
        PR(r.barrelSlosh, barrel->m_bodyTipTime == 0 && !barrel->m_bodyTippingBack && barrel->m_bodyTipping &&
                              SaSameQuat(body->m_rot, SaNormalized(barrel->m_bodyRot.Slerp(barrel->m_bodyTipRot, 1.0f, false))) &&
                              SaLogIs({"update body"}));
        // (The slerp the short way or not: from a rotation to one with a negative dot.)
        barrel->m_bodyRot = S_quat(1.0f, 0.0f, 0.0f, 0.0f);
        barrel->m_bodyTipRot = S_quat(-0.8f, 0.0f, 0.6f, 0.0f);
        barrel->m_bodyTippingBack = false;
        barrel->m_bodyTipTime = 200;
        barrel->Update(0);
        PR(r.barrelSlosh, SaSameQuat(body->m_rot, SaNormalized(barrel->m_bodyRot.Slerp(barrel->m_bodyTipRot, 0.5f, false))));
        barrel->m_bodyTipping = false;
        // The sway: an amount run down to exactly 0 is damped; damped, a phase of exactly 0.5 stops it on a
        // cosine over 0.5; the slerp's way; the surface updated.
        barrel->m_sloshing = true;
        barrel->m_sloshDamped = false;
        barrel->m_sloshAmount = 0.0f;
        barrel->m_sloshTime = 400;
        barrel->m_sloshPhase = 0.0f;
        reset();
        barrel->Update(0);
        PR(r.barrelSlosh, barrel->m_sloshDamped && barrel->m_sloshAmount == 0.0001f && SaLogIs({"update dekl"}));
        barrel->m_sloshing = true;
        barrel->m_sloshPhase = 0.5f;
        barrel->m_sloshTime = 100;
        barrel->Update(0);
        PR(r.barrelSlosh, !barrel->m_sloshing && barrel->m_sloshPhase == 0.0f);
        barrel->m_sloshing = true;
        barrel->m_sloshDamped = false;
        barrel->m_sloshAmount = 1.0f;
        barrel->m_sloshTime = 1200;
        barrel->m_sloshFrom = S_quat(1.0f, 0.0f, 0.0f, 0.0f);
        barrel->m_sloshTo = S_quat(-0.8f, 0.0f, 0.6f, 0.0f);
        g_lastSlerpFlag = true;
        barrel->Update(0);
        PR(r.barrelSlosh, !g_lastSlerpFlag);
        {
            const float c = std::cos(static_cast<float>(1200) * 0.00392699009f);
            const S_quat sway = barrel->m_sloshFrom.Slerp(barrel->m_sloshTo, c * 1.0f, false);
            PR(r.barrelSlosh, SaSameQuat(dekl->m_rot, SaNormalized(sway.RotateByMatrix(RotateYMatrix(
                                                          static_cast<float>(1200 - 400) * 0.00012f)))));
        }
        barrel->m_sloshing = false;

        // The leak: a hole exactly 0.09 under the level still leaks; a level at exactly the empty one is empty.
        barrel->m_empty = false;
        barrel->m_level = 0.6f;
        barrel->m_holes.push_back(new C_Barrel::S_Hole{0.6f - 0.09f, 700});
        reset();
        barrel->Update(10);
        PR(r.barrelLeak, barrel->m_holes.size() == 1 && barrel->m_level == 0.6f - 1.0f * barrel->m_leakRate * 10.0f * 0.001f);
        for (C_Barrel::S_Hole *h : barrel->m_holes)
            delete h;
        barrel->m_holes.clear();
        barrel->m_level = barrel->m_emptyLevel;
        barrel->Update(10);
        PR(r.barrelLeak, barrel->m_empty);
        barrel->m_empty = false;
        barrel->m_level = 0.5f;

        // The damage: none at exactly 0 left (even over the explosion level); 0.25 left is some; exactly
        // used up destroys; 0.4 left does not; at exactly the explosion level not destroyed.
        barrel->m_health = 0.0f;
        reset();
        barrel->Hit(HIT_MELEE, 1.0f, S_vector(bx, 0.5f, bz), S_vector(-1, 0, 0), nullptr, 0);
        PR(r.barrelDestroy, !barrel->m_empty && barrel->m_health == 0.0f);
        barrel->m_health = 0.25f;
        barrel->Hit(HIT_MELEE, 0.125f, S_vector(bx, 0.5f, bz), S_vector(-1, 0, 0), nullptr, 0);
        PR(r.barrelDestroy, barrel->m_health == 0.125f && !barrel->m_empty);
        barrel->m_health = 5.0f;
        barrel->Hit(HIT_MELEE, 4.5f, S_vector(bx, 0.5f, bz), S_vector(-1, 0, 0), nullptr, 0);
        PR(r.barrelDestroy, barrel->m_health == 0.5f && !barrel->m_empty);
        barrel->m_health = 0.75f;
        barrel->Hit(HIT_MELEE, 0.375f, S_vector(bx, 0.5f, bz), S_vector(-1, 0, 0), nullptr, 0);
        PR(r.barrelDestroy, barrel->m_health == 0.375f && !barrel->m_empty);
        barrel->m_health = 5.0f;
        barrel->m_level = barrel->m_explosionLevel;
        barrel->Hit(HIT_MELEE, 5.0f, S_vector(bx, 0.5f, bz), S_vector(-1, 0, 0), nullptr, 0);
        PR(r.barrelDestroy, barrel->m_health == 0.0f && !barrel->m_empty);
        barrel->m_level = 0.5f;
        barrel->m_health = 5.0f;
        reset();
        barrel->Hit(HIT_MELEE, 5.0f, S_vector(bx, 0.5f, bz), S_vector(-1, 0, 0), nullptr, 0);
        PR(r.barrelDestroy, barrel->m_health == 0.0f && barrel->m_empty);
        // (Destroyed by message: no hit points left.)
        barrel->StopMission(true);
        barrel->StartMission(true);
        barrel->m_health = 50.0f;
        g_readBits = {{2, 3}};
        g_readPos = 0;
        barrel->OnMessage(reinterpret_cast<C_BitStream *>(&g_readBits));
        PR(r.barrelDestroy, barrel->m_health == 0.0f);
        // The stop: the frame shown and updated, the wreck unlinked, updated, released; the body back.
        reset();
        barrel->StopMission(true);
        PR(r.barrelStop, SaLogIs({"on barrel 1", "update barrel", "link barrel_destroyed 0 0", "update barrel_destroyed",
                                  "release barrel_destroyed", "update body"}));
        barrel->StartMission(true);

        // A server's hits send nothing (the body's, the surface's); they do not tip the body.
        barrel->m_health = 1e6f;
        barrel->m_level = 0.9f;
        barrel->m_bodyTipping = false;
        reset();
        barrel->Hit(HIT_BULLET, 1.0f, S_vector(bx + 0.3f, 0.5f, bz), S_vector(-1, 0, 0), nullptr, 0);
        barrel->Hit(HIT_BULLET, 1.0f, S_vector(bx + 0.3f, 0.5f, bz), S_vector(-1, 0, 0), nullptr,
                    static_cast<int>(reinterpret_cast<intptr_t>(dekl)));
        PR(r.barrelHit, g_streams.empty() && !barrel->m_bodyTipping);
        // The surface hit's other side: the far side above the level, the near one leaks.
        {
            for (C_Barrel::S_Hole *h : barrel->m_holes)
                delete h;
            barrel->m_holes.clear();
            const S_vector pos(bx + 0.3f, 0.5f, bz), dir(-0.6f, 0.8f, 0.0f);
            reset();
            barrel->Hit(HIT_BULLET, 1.0f, pos, dir, nullptr, static_cast<int>(reinterpret_cast<intptr_t>(dekl)));
            PR(r.barrelHit, barrel->m_holes.size() == 1 && std::fabs(barrel->m_holes[0]->height - 0.5f) < 1e-5f &&
                                g_rbSplashes.size() == 1 && std::fabs(g_rbSplashes[0].pos.x - (bx + 0.3f)) < 1e-5f);
            // (Through the body steeply down: the far side under the barrel, no exit hole; along -z: the
            // exit at the far side.)
            reset();
            barrel->Hit(HIT_BULLET, 1.0f, S_vector(bx + 0.3f, 0.8f, bz), S_vector(-0.3f, -1.0f, 0.0f), nullptr, 0);
            PR(r.barrelHit, g_rbSplashes.size() == 1 && barrel->m_holes.size() == 2);
            reset();
            barrel->Hit(HIT_BULLET, 1.0f, S_vector(bx, 0.5f, bz + 0.3f), S_vector(0.0f, 0.0f, -1.0f), nullptr, 0);
            PR(r.barrelHit, g_rbSplashes.size() == 2 && std::fabs(g_rbSplashes[1].pos.z - (bz - 0.3f)) < 1e-5f);
            // (Straight down: no exit in the xz plane, none.)
            reset();
            barrel->Hit(HIT_BULLET, 1.0f, S_vector(bx, 0.9f, bz), S_vector(0.0f, -1.0f, 0.0f), nullptr,
                        static_cast<int>(reinterpret_cast<intptr_t>(dekl)));
            PR(r.barrelHit, g_rbSplashes.empty());
            for (C_Barrel::S_Hole *h : barrel->m_holes)
                delete h;
            barrel->m_holes.clear();
        }

        // A client's: reliable and sent; a bump's points: the body's and the surface's points away from the
        // other actor (levelled, normalised: a tiny x or z to 1, none to up) in their own spaces.
        g_gameRole = GAME_ROLE_CLIENT;
        reset();
        barrel->Hit(HIT_BULLET, 1.0f, S_vector(bx + 0.3f, 0.5f, bz), S_vector(-1, 0, 0), nullptr,
                    static_cast<int>(reinterpret_cast<intptr_t>(dekl)));
        PR(r.barrelNet, g_streams.size() == 1 && g_streams[0]->reliable && g_streams[0]->sent);
        auto *other = new C_Human();
        I3D_frame *otherFrame = NewFrame(FRAME_DUMMY, "bumper3", nullptr);
        other->m_frame = otherFrame;
        body->m_localMatrix(3) = S_vector(0.02f, 0.05f, -0.01f);
        body->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        const S_vector bodyAt = body->GetWorldPos(), deklAt = dekl->GetWorldPos();
        const S_vector froms[4] = {S_vector(bx - 1.0f, 0.7f, bz + 0.4f), S_vector(bodyAt.x, 3.0f, bodyAt.z),
                                   S_vector(bodyAt.x - 1e-5f, 0.2f, bodyAt.z), S_vector(bodyAt.x, 0.2f, bodyAt.z - 3e-6f)};
        for (const S_vector &from : froms) {
            otherFrame->m_localMatrix(3) = from;
            otherFrame->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
            S_vector away(bodyAt.x - from.x, 0.0f, bodyAt.z - from.z);
            PrNormalize(away, away.x * away.x + away.z * away.z);
            S_matrix inverse;
            inverse.Inverse(body->GetMatrix());
            const S_vector pos = S_vector(away.x + bodyAt.x, away.y + bodyAt.y, away.z + bodyAt.z) * inverse;
            S_vector toward(deklAt.x - from.x, 0.0f, deklAt.z - from.z);
            PrNormalize(toward, toward.z * toward.z + toward.x * toward.x);
            inverse.Inverse(dekl->GetMatrix());
            const S_vector dir = S_vector(toward.x + deklAt.x, toward.y + deklAt.y, toward.z + deklAt.z) * inverse;
            std::vector<std::pair<uint32_t, int>> expect = {{1u, 3}, {1u, 1}};
            for (float f : {pos.x, pos.y, pos.z, dir.x, dir.y, dir.z})
                expect.emplace_back(PrFloatBits(f), 32);
            reset();
            barrel->OnCollisionEx(other, nullptr, 0, 0, 0, 0, 0);
            PR(r.barrelNet, PrBits(0) == expect && g_streams[0]->sent);
        }
        delete other;
        body->m_localMatrix(3) = S_vector(0.0f, 0.05f, 0.0f);
        body->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        g_gameRole = GAME_ROLE_SERVER;

        // Saved with a single hit point: its chunk; read: a chunk after the hit points' read too.
        barrel->m_hitPoints = 1;
        barrel->m_health = 1.0f;
        const std::vector<uint8_t> bytes = SaWritten([&](C_chunk &c) { return barrel->SaveGame(c); }, nullptr);
        PR(r.barrelSave, bytes.size() == 6 + 6 + 10 + 7 + 10 + 10);
        barrel->m_hitPoints = 450;
        S_SaBytes in;
        in.Head(0x777, 6 + 6 + 10 + 10 + 10).Head(1, 6 + 10 + 10 + 10).Head(0x3039, 10).Val(g_actorSaveTag);
        in.Head(3000, 10).Val(40).Head(2000, 10).Val(0.65f);
        S_SaReader rd(in);
        barrel->LoadGame(rd.c);
        PR(r.barrelSave, barrel->m_health == 40.0f && barrel->m_level == 0.65f);
        barrel->StopMission(true);
    }
    g_rbSplashStep = 0;
    g_rbSplashHandle = 1;
    g_cacheFill = nullptr;
    frame->m_data = nullptr;
    creator->Destroy(barrel);
}
