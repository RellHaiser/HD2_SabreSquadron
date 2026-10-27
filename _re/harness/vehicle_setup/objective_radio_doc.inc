// (objective_actors_test.inc, inside ObjectiveActorsTest: the radio and the document place.)

    // --- the radio ---------------------------------------------------------------------------------------
    {
        C_ActorCreator *creator = g_actorCreators[ACTOR_RADIO];
        OB(r.radio, creator && std::strcmp(creator->GetName(), "Radio") == 0);
        for (int k = 0; k < 2; ++k) {
            I3D_frame *f = placed(FRAME_MODEL, "ob.radio", k == 0);
            if (k == 0)
                boxFrame(f, "a", 0);
            auto *a = static_cast<C_Radio *>(creator->Create());
            OB(r.radio, a->GetType() == ACTOR_RADIO && std::strcmp(*a->GetName(), "text 7160") == 0 && a->m_sound == -1 &&
                            a->GetItemId() == -1 && a->m_side == 2 && !a->m_started && a->m_volume.flags == 0);
            a->SetFrame(f);
            g_gameRole = GAME_ROLE_SERVER;
            reset();
            a->m_state = 7;
            a->StartMission(true);
            OB(r.radio, a->m_started && a->GetState() == 1 && g_volumeLog.size() == 1 && g_volumeLog[0].op == 'A' &&
                            g_volumeLog[0].volume == &a->m_volume && g_activations == std::vector<bool>{true});
            OB(r.radio, SaSameVolume(a->m_volume, a->m_invMatrix, f, COLLISION_TYPE_PLACE, a));
            // Started already: only the activation.
            a->m_state = 2;
            reset();
            a->StartMission(false);
            OB(r.radio, a->GetState() == 2 && g_volumeLog.empty() && g_activations == std::vector<bool>{false});
            reset();
            a->RestartMission(true);
            OB(r.radio, a->GetState() == 1 && g_activations.empty() && g_deactivations.empty() && g_volumeLog.empty());
            reset();
            a->StopMission(false);
            OB(r.radio, !a->m_started && g_volumeLog.size() == 1 && g_volumeLog[0].op == 'R' &&
                            g_volumeLog[0].volume == &a->m_volume && g_deactivations == std::vector<bool>{false});
            // CopyFrom: the base's, the sound, the item, the side (not the state).
            auto *b = static_cast<C_Radio *>(creator->Create());
            b->m_sound = 4;
            b->m_itemId = 9;
            b->m_side = 1;
            b->m_state = 2;
            const int copies = g_actorCopies;
            a->CopyFrom(b);
            OB(r.radio, g_actorCopies == copies + 1 && a->m_sound == 4 && a->m_itemId == 9 && a->m_side == 1 && a->GetState() == 1);
            creator->Destroy(b);
            // GetMaterialId: the part's (-1 none), else the frame's, else 11.
            I3D_frame *part = AnFrame(FRAME_VISUAL, "ob.rpart", f);
            const int partIds[] = {4, -1, -1, -2};
            const int frameIds[] = {6, 8, -1, 9};
            for (int i = 0; i < 4; ++i) {
                g_frameMaterials[part] = partIds[i];
                g_frameMaterials[f] = frameIds[i];
                OB(r.radio, a->GetMaterialId(part) == (partIds[i] != -1 ? partIds[i] : frameIds[i] != -1 ? frameIds[i] : 11));
                OB(r.radio, a->GetMaterialId(f) == (frameIds[i] != -1 ? frameIds[i] : 11));
            }
            g_frameMaterials.erase(part);
            g_frameMaterials.erase(f);
            creator->Destroy(a);
        }

        // CanUse / Use: the item when one is needed, the side (2 either); CanUse also needs state 1.
        // Use sets state 2 and tells everyone (2 bits, reliable).
        {
            I3D_frame *f = placed(FRAME_MODEL, "ob.radio", false);
            auto *a = static_cast<C_Radio *>(creator->Create());
            a->SetFrame(f);
            a->m_netId = 44;
            C_Item *item = ObItem(5, 77);
            const int items[] = {-1, 5, 0};
            const int sides[] = {2, 0, 1};
            for (int item0 : items)
                for (int side : sides)
                    for (int state = 0; state <= 3; ++state)
                        for (int carried = 0; carried < 2; ++carried)
                            for (int team = 0; team < 2; ++team) {
                                C_Human h;
                                h.team = team;
                                if (carried)
                                    h.items.emplace_back(item0 < 0 ? 5 : item0, item);
                                h.items.emplace_back(6, ObItem(6, 78));
                                a->m_itemId = item0;
                                a->m_side = side;
                                a->m_state = state;
                                const bool accepts = (item0 < 0 || carried) && (side == 2 || side == team);
                                OB(r.radioUse, a->CanUse(&h) == (state == 1 && accepts) && a->GetState() == state);
                                reset();
                                const bool used = a->Use(&h);
                                if (accepts)
                                    OB(r.radioUse, used && a->GetState() == 2 && g_streams.size() == 1 &&
                                                       g_streams[0]->id == 44 && g_streams[0]->sent &&
                                                       g_streams[0]->bits == T_Bits{{2, 2}});
                                else
                                    OB(r.radioUse, !used && a->GetState() == state && g_streams.empty());
                            }
            // SetState: the clients told (2 bits) unless the game is local.
            const uint8_t roles[] = {GAME_ROLE_LOCAL, GAME_ROLE_SERVER, GAME_ROLE_CLIENT};
            for (uint8_t role : roles) {
                g_gameRole = role;
                reset();
                a->SetState(3);
                OB(r.radioUse, a->GetState() == 3 &&
                                   (role == GAME_ROLE_LOCAL ? g_streams.empty()
                                                            : g_streams.size() == 1 && g_streams[0]->bits == T_Bits{{3, 2}} &&
                                                                  g_streams[0]->sent && g_streams[0]->id == 44));
            }
            // OnMessage: the state; 2 with a sound plays it (object manipulation, at the frame).
            const S_matrix &w = f->GetMatrix();
            const int sounds[] = {6, -1, 0};
            for (int sound : sounds)
                for (uint32_t state = 0; state < 4; ++state) {
                    a->m_sound = sound;
                    SaRead({{state, 2}});
                    reset();
                    a->OnMessage(nullptr);
                    const bool plays = state == 2 && sound >= 0;
                    OB(r.radioUse, SaReadAll() && a->GetState() == static_cast<int>(state) &&
                                       g_soundRequests.size() == (plays ? 1u : 0u) &&
                                       (!plays || sameSound(g_soundRequests[0], w(3), w.m[3][3], SOUND_CHAPTER_OBJECT_MANIPULATION, sound)));
                }
            creator->Destroy(a);
        }

        // Properties: chunk 100 the sound and the item, 101 the side (not left); another id entered and
        // not left. The saved game: chunk 3000 the state.
        {
            auto *a = static_cast<C_Radio *>(creator->Create());
            a->m_sound = 12;
            a->m_itemId = 34;
            a->m_side = 1;
            bool written = false;
            S_SaBytes e;
            e.Head(0x777, 30).Head(100, 14).Val(12).Val(34).Head(101, 10).Val(1);
            OB(r.radioSave, SaWritten([&](C_chunk &c) { return a->WriteProperties(c); }, &written) == e.b && written);
            S_SaBytes in;
            in.Head(0x777, 6 + 14 + 10 + 10).Head(100, 14).Val(3).Val(8).Head(101, 10).Val(0).Head(100, 10).Val(55);
            S_SaReader rd(in);
            OB(r.radioSave, a->ReadProperties(rd.c) && a->m_sound == 3 && a->m_itemId == 8 && a->m_side == 0 && rd.c.m_depth == 2);
            S_SaBytes nested;
            nested.Head(0x777, 6 + 20 + 10).Head(9, 20).Head(100, 14).Val(1).Val(2).Head(100, 10).Val(66);
            S_SaReader rd2(nested);
            OB(r.radioSave, a->ReadProperties(rd2.c) && a->m_sound == 1 && a->m_itemId == 2 && rd2.c.m_depth == 2);   // (the 66 is outside the entered chunk)
            a->m_state = 2;
            S_SaBytes sv;
            sv.Head(0x777, 16).Head(3000, 10).Val(2);
            bool saved = false;
            OB(r.radioSave, SaWritten([&](C_chunk &c) { return a->SaveGame(c); }, &saved) == sv.b && saved);
            S_SaBytes ld;
            ld.Head(0x777, 22).Head(4, 16).Head(3000, 10).Val(1);
            S_SaReader rl(ld);
            OB(r.radioSave, a->LoadGame(rl.c) && a->GetState() == 1 && rl.c.m_depth == 2);
            S_SaBytes sh;
            sh.Head(0x777, 14).Head(3000, 8).Val(static_cast<uint16_t>(3));
            S_SaReader rs(sh);
            OB(r.radioSave, a->LoadGame(rs.c) && a->GetState() == 1);
            creator->Destroy(a);
        }
    }

    // --- the document place ---------------------------------------------------------------------------------
    {
        C_ActorCreator *creator = g_actorCreators[ACTOR_DOCUMENT_PLACE];
        OB(r.doc, creator && std::strcmp(creator->GetName(), "Document place") == 0);
        C_ItTestDef docDef;
        docDef.m_textId = 4321;
        I3D_frame *docModel = AnFrame(FRAME_MODEL, "ob.docmodel", nullptr);
        g_itemModel = docModel;
        g_obDefs[0x44] = &docDef;
        // The log of ShowDocuments (made once).
        const auto showLog = [&](C_DocumentPlace *a, bool first) {
            std::vector<std::string> log;
            if (first)
                log.push_back("duplicate visual copy " + ItName(docModel));
            const std::string frame = ItName(a->GetFrame());
            log.push_back("link visual copy " + frame + " 0");
            log.push_back("worlddir visual copy 0 0 1 0");
            log.push_back("on visual copy 1");
            log.push_back("update visual copy");
            log.push_back("update " + frame);
            log.push_back("link visual copy 0 1");
            return log;
        };
        for (int k = 0; k < 2; ++k) {
            I3D_frame *f = placed(FRAME_MODEL, "ob.doc", k == 0);
            boxFrame(f, "a", k);
            auto *a = static_cast<C_DocumentPlace *>(creator->Create());
            OB(r.doc, a->GetType() == ACTOR_DOCUMENT_PLACE && std::strcmp(*a->GetName(), "Missing actor name!!") == 0 &&
                          a->m_nameTextId == 0x4E7 && a->GetItemId() == 0x44 && a->GetTextId() == 0x1B8B &&
                          a->GetState() == DOCUMENTS_STOPPED && !a->m_started && !a->m_volumeAdded && !a->m_documents);
            a->SetFrame(f);
            a->m_netId = 51;
            g_gameRole = GAME_ROLE_LOCAL;
            // StartMission: the volume made (not added by itself), waiting: the name, the frame shown,
            // the volume added.
            reset();
            a->StartMission(true);
            OB(r.doc, a->m_started && a->GetState() == DOCUMENTS_WAITING && std::strcmp(*a->GetName(), "text 1255") == 0 &&
                          a->m_volumeAdded && g_volumeLog.size() == 1 && g_volumeLog[0].op == 'A' &&
                          g_volumeLog[0].volume == &a->m_volume && SaLogIs({"on " + ItName(f) + " 1"}) &&
                          g_activations == std::vector<bool>{true});
            OB(r.doc, SaSameVolume(a->m_volume, a->m_invMatrix, f, COLLISION_TYPE_LIGHT_OBJECT, a));
            // Started: only the activation (the state as it was).
            a->m_state = DOCUMENTS_HIDDEN;
            reset();
            a->StartMission(false);
            OB(r.doc, g_itLog.empty() && g_volumeLog.empty() && g_activations == std::vector<bool>{false} &&
                          a->GetState() == DOCUMENTS_HIDDEN);
            // Waiting again with the volume in: not added twice.
            reset();
            a->ApplyState(DOCUMENTS_WAITING);
            OB(r.docState, a->GetState() == DOCUMENTS_WAITING && a->m_volumeAdded && g_volumeLog.empty() &&
                               SaLogIs({"on " + ItName(f) + " 1"}));
            // Delivered: the item's name, the documents shown, the volume out.
            reset();
            a->ApplyState(DOCUMENTS_DELIVERED);
            std::vector<std::string> log = showLog(a, true);
            log.push_back("sector visual copy " + ItV(a->m_documents ? a->m_documents->GetWorldPos() : S_vector(0, 0, 0)));
            log.push_back("update visual copy");
            log.push_back("update " + ItName(f));
            log.push_back("on " + ItName(f) + " 0");
            OB(r.docState, a->GetState() == DOCUMENTS_DELIVERED && std::strcmp(*a->GetName(), "text 4321") == 0 &&
                               a->m_documents && !a->m_volumeAdded && g_volumeLog.size() == 1 && g_volumeLog[0].op == 'R' &&
                               SaLogIs(log) && !a->m_documents->GetParent() &&
                               RbSame(a->m_documents->GetWorldPos(), f->GetWorldPos()));   // (left where the place is)
            I3D_frame *docs = a->m_documents;
            // Again: nothing. Shown again (ReadState): the model reused.
            reset();
            a->ApplyState(DOCUMENTS_DELIVERED);
            OB(r.docState, g_itLog.empty());
            SaRead({{1, 2}});
            reset();
            a->ReadState(nullptr);
            std::vector<std::string> again = showLog(a, false);
            again.push_back("sector visual copy " + ItV(docs->GetWorldPos()));
            again.push_back("update visual copy");
            again.push_back("update " + ItName(f));
            again.push_back("on " + ItName(f) + " 0");
            OB(r.docState, SaReadAll() && a->m_documents == docs && SaLogIs(again) && a->GetState() == DOCUMENTS_DELIVERED);
            SaRead({{0, 2}});
            reset();
            a->ReadState(nullptr);
            OB(r.docState, SaReadAll() && g_itLog.empty());
            // Hidden: the documents and the frame off (the volume is not in).
            reset();
            a->ApplyState(DOCUMENTS_HIDDEN);
            OB(r.docState, a->GetState() == DOCUMENTS_HIDDEN && SaLogIs({"on visual copy 0", "on " + ItName(f) + " 0"}) &&
                               g_volumeLog.empty());
            // Waiting again, then hidden: the volume out.
            reset();
            a->ApplyState(DOCUMENTS_WAITING);
            a->ApplyState(DOCUMENTS_HIDDEN);
            OB(r.docState, g_volumeLog.size() == 2 && g_volumeLog[0].op == 'A' && g_volumeLog[1].op == 'R' && !a->m_volumeAdded);
            // An unknown state: kept, nothing else.
            reset();
            a->ApplyState(9);
            OB(r.docState, a->GetState() == 9 && g_itLog.empty() && g_volumeLog.empty());
            // WriteState: 2 bits.
            C_BitStream *ws = newStream();
            a->m_state = 2;
            a->WriteState(ws);
            OB(r.docNet, lastStream()->bits == T_Bits{{2, 2}});
            // StopMission: stopped (the documents let go); then only the deactivation.
            a->ApplyState(DOCUMENTS_WAITING);
            reset();
            a->StopMission(true);
            OB(r.docState, a->GetState() == DOCUMENTS_STOPPED && !a->m_documents && !a->m_started && !a->m_volumeAdded &&
                               g_volumeLog.size() == 1 && g_volumeLog[0].op == 'R' && SaLogIs({"release visual copy"}) &&
                               g_deactivations == std::vector<bool>{true});
            a->m_state = DOCUMENTS_HIDDEN;
            reset();
            a->StopMission(false);
            OB(r.docState, g_itLog.empty() && g_volumeLog.empty() && g_deactivations == std::vector<bool>{false} &&
                               a->GetState() == DOCUMENTS_HIDDEN);
            // Stopped with the volume out: not removed again.
            reset();
            a->ApplyState(DOCUMENTS_STOPPED);
            OB(r.docState, a->GetState() == DOCUMENTS_STOPPED && g_volumeLog.empty() && g_itLog.empty());
            // RestartMission: each side the game runs stopped and started.
            const uint8_t roles[] = {GAME_ROLE_CLIENT, GAME_ROLE_SERVER, 3};
            for (uint8_t role : roles) {
                g_gameRole = role;
                reset();
                a->RestartMission(true);
                std::vector<bool> stops, starts;
                if (role & GAME_ROLE_CLIENT)
                    stops.push_back(false);
                if (role & GAME_ROLE_SERVER) {
                    stops.push_back(true);
                    starts.push_back(true);
                }
                if (role & GAME_ROLE_CLIENT)
                    starts.push_back(false);
                OB(r.docState, g_deactivations == stops && g_activations == starts && a->GetState() == DOCUMENTS_WAITING);
            }
            g_gameRole = GAME_ROLE_LOCAL;
            OB(r.doc, a->GetMaterialId(f) == 85);
            // The destructor lets the documents go (Fix).
            a->ApplyState(DOCUMENTS_DELIVERED);
            reset();
            creator->Destroy(a);
            OB(r.doc, std::find(g_itLog.begin(), g_itLog.end(), "release visual copy") != g_itLog.end());
        }
        // Without the item's definition: the name of text -1, nothing shown (Fix).
        {
            I3D_frame *f = placed(FRAME_MODEL, "ob.doc", false);
            auto *a = static_cast<C_DocumentPlace *>(creator->Create());
            a->SetFrame(f);
            a->m_itemId = 0x99;
            a->StartMission(true);
            reset();
            a->ApplyState(DOCUMENTS_DELIVERED);
            OB(r.docState, std::strcmp(*a->GetName(), "text -1") == 0 && !a->m_documents && g_itLog.empty() &&
                               g_volumeLog.size() == 1);
            creator->Destroy(a);
        }

        // Deliver / SetState / OnMessage.
        {
            I3D_frame *f = placed(FRAME_MODEL, "ob.doc", true);
            auto *a = static_cast<C_DocumentPlace *>(creator->Create());
            a->SetFrame(f);
            a->m_netId = 52;
            g_gameRole = GAME_ROLE_SERVER;
            a->StartMission(true);
            C_Human h;
            h.m_netId = 17;
            C_Item *docs = ObItem(0x44, 0x1234);
            for (int k = 0; k < 3; ++k) {
                a->ApplyState(k == 2 ? DOCUMENTS_HIDDEN : DOCUMENTS_WAITING);
                h.items.clear();
                h.removedItems.clear();
                if (k != 1)
                    h.items.emplace_back(0x44, docs);
                h.items.emplace_back(0x45, ObItem(0x45, 3));
                reset();
                const bool ok = a->Deliver(&h);
                if (k == 0)
                    OB(r.docNet, ok && a->GetState() == DOCUMENTS_DELIVERED && h.removedItems.size() == 1 &&
                                     h.removedItems[0] == std::make_pair(0x1234, true) && g_streams.size() == 1 &&
                                     g_streams[0]->id == 52 && g_streams[0]->sent &&
                                     g_streams[0]->bits == T_Bits{{0, 32}, {17, 10}, {0x1234, 16}});
                else
                    OB(r.docNet, !ok && h.removedItems.empty() && g_streams.empty());
            }
            // SetState: the clients told (the kind 1 in a word, the state in 2 bits) unless local.
            const uint8_t roles[] = {GAME_ROLE_SERVER, GAME_ROLE_LOCAL};
            for (uint8_t role : roles) {
                g_gameRole = role;
                a->ApplyState(DOCUMENTS_WAITING);
                reset();
                a->SetState(DOCUMENTS_HIDDEN);
                OB(r.docNet, a->GetState() == DOCUMENTS_HIDDEN &&
                                 (role == GAME_ROLE_LOCAL ? g_streams.empty()
                                                          : g_streams.size() == 1 && g_streams[0]->bits == T_Bits{{1, 32}, {2, 2}} &&
                                                                g_streams[0]->sent));
                reset();
                a->SetState(DOCUMENTS_HIDDEN);
                OB(r.docNet, g_streams.empty() && g_itLog.empty());
            }
            // OnMessage (clients): always a delivery (the kind's word skipped): the human's item by uid
            // out of his inventory, delivered.
            C_Human remote;
            remote.items.emplace_back(0x44, ObItem(0x44, -5));
            g_obHumans[23] = &remote;
            const uint8_t msgRoles[] = {GAME_ROLE_CLIENT, GAME_ROLE_SERVER};
            for (uint8_t role : msgRoles)
                for (int k = 0; k < 4; ++k) {
                    g_gameRole = role;
                    a->ApplyState(DOCUMENTS_WAITING);
                    remote.removedItems.clear();
                    const uint32_t kind = k == 3 ? 1 : 0;
                    const uint32_t id = k == 1 ? 24 : 23;
                    const uint32_t uid = k == 2 ? 0x1111 : 0xFFFB;   // (-5 in 16 bits)
                    SaRead({{kind, 32}, {id, 10}, {uid, 16}});
                    reset();
                    a->OnMessage(nullptr);
                    const bool delivered = role == GAME_ROLE_CLIENT && k != 1 && k != 2;
                    OB(r.docNet, SaReadAll() && a->GetState() == (delivered ? DOCUMENTS_DELIVERED : DOCUMENTS_WAITING) &&
                                     remote.removedItems.size() == (delivered ? 1u : 0u) &&
                                     (!delivered || remote.removedItems[0] == std::make_pair(-5, true)) &&
                                     (role != GAME_ROLE_CLIENT ? std::find(g_itLog.begin(), g_itLog.end(), "gethuman 23") == g_itLog.end()
                                                               : !g_itLog.empty()));
                }
            g_obHumans.clear();
            creator->Destroy(a);
        }

        // Properties: chunk 100 the name's text and the item, 101 the signal's text (both left); another
        // id entered and not left. The saved game: chunk 3000 the state (3 when there is none).
        {
            I3D_frame *f = placed(FRAME_MODEL, "ob.doc", false);
            auto *a = static_cast<C_DocumentPlace *>(creator->Create());
            a->SetFrame(f);
            a->m_nameTextId = 11;
            a->m_itemId = 22;
            a->m_textId = 33;
            bool written = false;
            S_SaBytes e;
            e.Head(0x777, 30).Head(100, 14).Val(11).Val(22).Head(101, 10).Val(33);
            OB(r.docSave, SaWritten([&](C_chunk &c) { return a->WriteProperties(c); }, &written) == e.b && written);
            S_SaBytes in;
            in.Head(0x777, 6 + 10 + 14 + 16).Head(101, 10).Val(7).Head(100, 14).Val(5).Val(6).Head(8, 16).Head(101, 10).Val(9);
            S_SaReader rd(in);
            OB(r.docSave, a->ReadProperties(rd.c) && a->m_nameTextId == 5 && a->GetItemId() == 6 && a->GetTextId() == 9 &&
                              rd.c.m_depth == 2);
            auto *b = static_cast<C_DocumentPlace *>(creator->Create());
            const int copies = g_actorCopies;
            b->CopyFrom(a);
            OB(r.doc, g_actorCopies == copies + 1 && b->m_nameTextId == 5 && b->GetItemId() == 6 && b->GetTextId() == 9);
            creator->Destroy(b);
            a->m_itemId = 0x44;
            a->StartMission(true);
            a->ApplyState(DOCUMENTS_HIDDEN);
            S_SaBytes sv;
            sv.Head(0x777, 16).Head(3000, 10).Val(DOCUMENTS_HIDDEN);
            bool saved = false;
            OB(r.docSave, SaWritten([&](C_chunk &c) { return a->SaveGame(c); }, &saved) == sv.b && saved);
            S_SaBytes ld;
            ld.Head(0x777, 22).Head(4, 16).Head(3000, 10).Val(DOCUMENTS_WAITING);
            S_SaReader rl(ld);
            reset();
            OB(r.docSave, a->LoadGame(rl.c) && a->GetState() == DOCUMENTS_WAITING && rl.c.m_depth == 2 &&
                              g_volumeLog.size() == 1 && g_volumeLog[0].op == 'A');
            S_SaBytes none;
            none.Head(0x777, 6);
            S_SaReader rn(none);
            reset();
            OB(r.docSave, a->LoadGame(rn.c) && a->GetState() == DOCUMENTS_STOPPED && g_volumeLog.size() == 1 &&
                              g_volumeLog[0].op == 'R');
            creator->Destroy(a);
        }
        g_obDefs.clear();
        g_itemModel = nullptr;
    }
