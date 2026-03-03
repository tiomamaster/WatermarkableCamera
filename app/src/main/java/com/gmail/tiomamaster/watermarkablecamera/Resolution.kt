package com.gmail.tiomamaster.watermarkablecamera

import android.util.Size

enum class Resolution(
    val title: String,
    val size: Size,
) {
    QHD("QuadHD", Size(2560, 1440)),
    FHD("FullHD", Size(1920, 1080)),
    HD("HD", Size(1280, 720)),
    SD576("SD 576p", Size(1024, 576)),
    SD480("SD 480p", Size(854, 480))
}