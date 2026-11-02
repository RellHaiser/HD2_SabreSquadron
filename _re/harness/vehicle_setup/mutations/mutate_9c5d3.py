"""Part 9c-v-d-iii mutations: the movie control (game/controls.cpp).

Run with the text build: --build build_text.bat --check check_text.py (TEXT_TEST; the "movie:"
checks in movie_test.inc). Generated (deletions, comparison, number, bool and negation flips).

67 of 71 build; all caught but 3 equivalent (line numbers as generated): the stream cleared in
CloseStream (K 5401 del: only the destructor calls it) and before Create's base (K 5406 del, K 5408
bool: C_Control::Create cannot fail, the stream then set either way).
"""
import os

ROOT = r"G:\Programming\HD2 Source Port\C++"
K = os.path.join(ROOT, r"src/game/controls.cpp")
C = os.path.join(ROOT, r"src/game/control.cpp")

MUTATIONS = [
    (K, 'K 5385 num', 'constexpr float MOVIE_Y_SCALE = -1.3333334f;\n', 'constexpr float MOVIE_Y_SCALE = -2.0000001f;\n'),
    (K, 'K 5386 num', 'constexpr int DRIVER_STATE_3 = 3, RENDER_STATE_0 = 0;\n', 'constexpr int DRIVER_STATE_3 = 4, RENDER_STATE_0 = 0;\n'),
    (K, 'K 5392 del', '    CloseStream();\n', '\n'),
    (K, 'K 5397 not', '    if (!m_stream)\n        return;\n', '    if (m_stream)\n        return;\n'),
    (K, 'K 5399 del', '    m_stream->Close();\n    m_stream->Release();\n', '\n    m_stream->Release();\n'),
    (K, 'K 5400 del', '    m_stream->Release();\n', '\n'),
    (K, 'K 5401 del', '    m_stream = nullptr;\n}\n', '\n}\n'),
    (K, 'K 5406 del', '    m_stream = nullptr;\n    if (!C_Control::Create(frame, root, desc))\n', '\n    if (!C_Control::Create(frame, root, desc))\n'),
    (K, 'K 5407 not', '    if (!C_Control::Create(frame, root, desc))\n        return false;\n    m_frame->SetOn(false);\n', '    if (C_Control::Create(frame, root, desc))\n        return false;\n    m_frame->SetOn(false);\n'),
    (K, 'K 5408 bool', '        return false;\n    m_frame->SetOn(false);\n', '        return true;\n    m_frame->SetOn(false);\n'),
    (K, 'K 5409 del', '    m_frame->SetOn(false);\n    m_stream = GetIGraph()->CreateStream();\n', '\n    m_stream = GetIGraph()->CreateStream();\n'),
    (K, 'K 5410 del', '    m_stream = GetIGraph()->CreateStream();\n', '\n'),
    (K, 'K 5411 not', '    if (!m_stream)\n        return false;\n', '    if (m_stream)\n        return false;\n'),
    (K, 'K 5412 bool', '        return false;\n    FindArea();\n', '        return true;\n    FindArea();\n'),
    (K, 'K 5413 del', '    FindArea();\n', '\n'),
    (K, 'K 5414 bool', '    FindArea();\n    return true;\n', '    FindArea();\n    return false;\n'),
    (K, 'K 5419 not', '    if (!m_frame)\n', '    if (m_frame)\n'),
    (K, 'K 5420 bool', '        return false;\n    const I3D_bbox bound = static_cast<I3D_visual *>(m_frame)->GetWorldBound();\n', '        return true;\n    const I3D_bbox bound = static_cast<I3D_visual *>(m_frame)->GetWorldBound();\n'),
    (K, 'K 5424 del', '    m_left = bound.min.x * width;\n', '\n'),
    (K, 'K 5425 del', '    m_bottom = bound.min.y * height * MOVIE_Y_SCALE;\n', '\n'),
    (K, 'K 5426 del', '    m_right = width * bound.max.x;\n', '\n'),
    (K, 'K 5427 del', '    m_top = height * bound.max.y * MOVIE_Y_SCALE;\n', '\n'),
    (K, 'K 5428 del', '    m_width = m_right - m_left;\n', '\n'),
    (K, 'K 5429 del', '    m_height = m_bottom - m_top;\n', '\n'),
    (K, 'K 5430 bool', '    m_height = m_bottom - m_top;\n    return true;\n', '    m_height = m_bottom - m_top;\n    return false;\n'),
    (K, 'K 5435 del', '    S_showRect size = {};\n', '\n'),
    (K, 'K 5436 del', '    m_stream->GetVideoSize(&size);\n', '\n'),
    (K, 'K 5439 del', '    S_showRect rect = {};\n', '\n'),
    (K, 'K 5442 cmp', '    if (m_height / m_width >= videoHeight / videoWidth) {\n', '    if (m_height / m_width > videoHeight / videoWidth) {\n'),
    (K, 'K 5443 del', '        rect.x = static_cast<int>(m_left);\n', '\n'),
    (K, 'K 5444 del', '        rect.width = static_cast<int>(m_width);\n', '\n'),
    (K, 'K 5445 del', '        rect.height = static_cast<int>(m_width / videoWidth * videoHeight);\n', '\n'),
    (K, 'K 5446 del', '        rect.y = static_cast<int>(m_top);\n    } else {\n', '\n    } else {\n'),
    (K, 'K 5448 del', '        rect.y = static_cast<int>(m_top);\n        rect.height = static_cast<int>(m_height);\n', '\n        rect.height = static_cast<int>(m_height);\n'),
    (K, 'K 5449 del', '        rect.height = static_cast<int>(m_height);\n', '\n'),
    (K, 'K 5450 del', '        rect.width = static_cast<int>(m_height / videoHeight * videoWidth);\n', '\n'),
    (K, 'K 5451 del', '        rect.x = static_cast<int>((m_width - static_cast<float>(rect.width)) * 0.5f + m_left);\n', '\n'),
    (K, 'K 5451 num', '        rect.x = static_cast<int>((m_width - static_cast<float>(rect.width)) * 0.5f + m_left);\n', '        rect.x = static_cast<int>((m_width - static_cast<float>(rect.width)) * 0.75f + m_left);\n'),
    (K, 'K 5453 del', '    m_stream->SetRect(&rect);\n', '\n'),
    (K, 'K 5458 not', '    if (!m_stream->IsPlaying())\n', '    if (m_stream->IsPlaying())\n'),
    (K, 'K 5460 del', '    m_stream->UpdateVideo(0);\n', '\n'),
    (K, 'K 5460 num', '    m_stream->UpdateVideo(0);\n', '    m_stream->UpdateVideo(1);\n'),
    (K, 'K 5461 del', '    I3D_driver *driver = I3DGetDriver();\n', '\n'),
    (K, 'K 5462 cmp', '    const bool state3 = driver->GetState(DRIVER_STATE_3) != 0;\n', '    const bool state3 = driver->GetState(DRIVER_STATE_3) == 0;\n'),
    (K, 'K 5462 num', '    const bool state3 = driver->GetState(DRIVER_STATE_3) != 0;\n', '    const bool state3 = driver->GetState(DRIVER_STATE_3) != 1;\n'),
    (K, 'K 5463 del', '    driver->SetState(DRIVER_STATE_3, 0);\n', '\n'),
    (K, 'K 5463 num', '    driver->SetState(DRIVER_STATE_3, 0);\n', '    driver->SetState(DRIVER_STATE_3, 1);\n'),
    (K, 'K 5465 del', '    m_stream->Draw(false);\n', '\n'),
    (K, 'K 5466 del', '    GetIGraph()->SetRenderState(RENDER_STATE_0, renderState);\n', '\n'),
    (K, 'K 5467 del', '    I3DGetDriver()->SetState(DRIVER_STATE_3, state3);\n', '\n'),
    (K, 'K 5469 cmp', '    if (state == ISNDSTREAM_STATE_FINISHED || state == -1) {\n', '    if (state != ISNDSTREAM_STATE_FINISHED || state == -1) {\n'),
    (K, 'K 5469 num', '    if (state == ISNDSTREAM_STATE_FINISHED || state == -1) {\n', '    if (state == ISNDSTREAM_STATE_FINISHED || state == -2) {\n'),
    (K, 'K 5470 del', '        m_stream->Stop();\n        FitRect();\n', '\n        FitRect();\n'),
    (K, 'K 5471 del', '        FitRect();\n        m_stream->Play();\n', '\n        m_stream->Play();\n'),
    (K, 'K 5472 del', '        m_stream->Play();\n    }\n', '\n    }\n'),
    (K, 'K 5482 del', '        m_stream->Stop();\n        m_stream->Close();\n', '\n        m_stream->Close();\n'),
    (K, 'K 5483 del', '        m_stream->Close();\n', '\n'),
    (K, 'K 5484 bool', '        return true;\n    case CONTROL_MSG_STOP_MOVIE:\n', '        return false;\n    case CONTROL_MSG_STOP_MOVIE:\n'),
    (K, 'K 5486 del', '        m_stream->Stop();\n        return true;\n', '\n        return true;\n'),
    (K, 'K 5487 bool', '        return true;\n    case CONTROL_MSG_PLAY_MOVIE: {\n', '        return false;\n    case CONTROL_MSG_PLAY_MOVIE: {\n'),
    (K, 'K 5489 del', '        const C_str name = C_fstr("GameMovie\\\\%s2", message->text.c_str());\n', '\n'),
    (K, 'K 5491 cmp', '        if (m_stream->Open(name.c_str(), 0) >= 0 || !m_stream->IsPlaying()) {\n', '        if (m_stream->Open(name.c_str(), 0) > 0 || !m_stream->IsPlaying()) {\n'),
    (K, 'K 5491 num', '        if (m_stream->Open(name.c_str(), 0) >= 0 || !m_stream->IsPlaying()) {\n', '        if (m_stream->Open(name.c_str(), 1) >= 0 || !m_stream->IsPlaying()) {\n'),
    (K, 'K 5491 not', '        if (m_stream->Open(name.c_str(), 0) >= 0 || !m_stream->IsPlaying()) {\n', '        if (m_stream->Open(name.c_str(), 0) >= 0 || m_stream->IsPlaying()) {\n'),
    (K, 'K 5492 del', '            FitRect();\n', '\n'),
    (K, 'K 5493 del', '            m_stream->SetVolume(0.0f);\n', '\n'),
    (K, 'K 5493 num', '            m_stream->SetVolume(0.0f);\n', '            m_stream->SetVolume(0.25f);\n'),
    (K, 'K 5494 del', '            m_stream->Play();\n', '\n'),
    (K, 'K 5496 bool', '        return true;\n    }\n    default:\n', '        return false;\n    }\n    default:\n'),
    (K, 'K 5499 del', '    }\n    default:\n        C_Control::HandleMessage(message);\n', '    }\n    default:\n\n'),
    (K, 'K 5500 bool', '    }\n    default:\n        C_Control::HandleMessage(message);\n        return true;\n', '    }\n    default:\n        C_Control::HandleMessage(message);\n        return false;\n'),
]
