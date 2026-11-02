plugins {
    alias(libs.plugins.androidApplication) apply false
}

allprojects {
    repositories {
        google()
        mavenCentral()
    }
}