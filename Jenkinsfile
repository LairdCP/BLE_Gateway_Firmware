pipeline {
    agent {
        label 'USDC01BD06'
    }
    options {
        skipDefaultCheckout true
        buildDiscarder logRotator(
            artifactDaysToKeepStr: '',
            artifactNumToKeepStr: '',
            daysToKeepStr: '30',
            numToKeepStr: ''
        )
        timestamps()
        disableConcurrentBuilds()
    }
    triggers {
        pollSCM '* * * * *'
    }
    environment {
        BUILD_DIR = "build"
        APP_SOURCE = "oob_demo\\oob_demo"
        BUILD_RESULT_HEX_FILE = "${env.BUILD_DIR}\\zephyr\\zephyr.hex"
        VERSION_INFO_FILE_PATH = "oob_demo/oob_demo/include/oob_common.h"
        FINAL_RESULT_NAME = "oob_demo"
    }
    stages {
        stage('West init') {
            steps {
                cleanWs()
                bat "west init -m ${scm.getUserRemoteConfigs()[0].getUrl()}"
            }
        }
        stage('West update') {
            steps {
                bat "west update"
            }
        }
        stage('Build') {
            steps{
                bat "west build -b pinnacle_100_dvk -d ${env.BUILD_DIR} ${env.APP_SOURCE} -- -D BOARD_ROOT=%cd%\\oob_demo"
            }
        }
        stage('Package') {
            steps {
                script {
                    def version_info_file = readFile "${env.VERSION_INFO_FILE_PATH}"
                    def lines = version_info_file.split("\n")
                    def file_version_major
                    def file_version_minor
                    def file_version_patch

                    lines.each {
                        if (it =~ /#define APP_VERSION_MAJOR/) {
                            file_version_major = it.findAll("\\d+")
                        }
                        else if (it =~ /#define APP_VERSION_MINOR/) {
                            file_version_minor = it.findAll("\\d+")
                        }
                        else if (it =~ /#define APP_VERSION_PATCH/) {
                            file_version_patch = it.findAll("\\d+")
                        }
                    }

                    def fwVersionMajor = file_version_major[0]
                    def fwVersionMinor = file_version_minor[0]
                    def fwVersionPatch = file_version_patch[0]

                    def full_revision = fwVersionMajor + "." + fwVersionMinor + "." + fwVersionPatch
                    def final_file_name = "${env.FINAL_RESULT_NAME}" + "-R" + full_revision + ".hex"

                    bat """
                        copy ${env.BUILD_RESULT_HEX_FILE} ${env.BUILD_DIR}\\${final_file_name}
                    """
                }
            }
        }
        stage('Archive Artifacts'){
            steps{
                archiveArtifacts "${env.BUILD_DIR}/*.hex"
            }
        }
    }
}