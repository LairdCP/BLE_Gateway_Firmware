pipeline {
    agent {
        // Build on this server
        label 'USDC01BD06'
    }
    options {
        // west handles checkout
        skipDefaultCheckout true
        // keep 30 days of builds
        buildDiscarder logRotator(
            artifactDaysToKeepStr: '',
            artifactNumToKeepStr: '',
            daysToKeepStr: '30',
            numToKeepStr: ''
        )
        // we want timestamps in the logs
        timestamps()
        // disallow more than one concurrent build of this branch
        disableConcurrentBuilds()
    }
    triggers {
        // check as often as possible for SCM changes
        pollSCM '* * * * *'
    }
    environment {
        // Set to URL of first git remote config from Multibranch Pipeline definition in Jenkins
        GIT_REPO = scm.getUserRemoteConfigs()[0].getUrl()

        APP_SOURCE = "mg100_firmware\\mg100"
        VERSION_FILE = "${env.APP_SOURCE}/include/mg100_common.h"

        BUILD_DIR = "build"
        BUILD_RESULT_HEX_FILE = "%WORKSPACE%\\${env.BUILD_DIR}\\zephyr\\zephyr.hex"

        // Path to zephyr scripts
        ZEPHYR_SCRIPTS = "zephyr/scripts"

        // Name of artifact directory to be created (relative to workspace)
        ARTIFACT_DIR = "artifacts"

        // Prefix used to name final artifacts
        ARTIFACT_PREFIX = "480-00052"

        // Final artifact files relative to workspace
        ARTIFACT_APP_HEX = "%WORKSPACE%\\%ARTIFACT_DIR%\\%ARTIFACT_PREFIX%-R%VERSION%.hex"

        // Pattern of artifacts to archive (relative to ARTIFACT_DIR)
        ARTIFACT_PATTERN = "*.hex"
    }
    stages {
        stage('Configure') {
            steps {
                // Clean the workspace
                cleanWs()

                // Init the repo
                bat 'west init -m "%GIT_REPO%"'

                // this block sets the VERSION env variable
                script {
                    // Read in the version file
                    def version_info_file = readFile "${env.VERSION_FILE}"
                    // Get each line of the version file
                    def lines = version_info_file.split("\n")

                    def file_version_major
                    def file_version_minor
                    def file_version_patch

                    // Search the lines for each version number field
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

                    def full_revision = file_version_major[0] + "." + file_version_minor[0] + "." + file_version_patch[0]

                    // Set version env variable.
                    // This also appends the build number to the 3 field version.
                    // The resulting version will look like: major.minor.patch.build or 1.0.0.1
                    env.VERSION = [full_revision, env.BUILD_NUMBER].join('.')
                }

                // set the Jenkins build name to the version name
                buildName env.VERSION
            }
        }
        stage('West Update') {
            steps {
                // Fetch all supporting repositories
                bat "west update"
            }
        }
        stage('Upgrade Dependencies') {
            steps {
                // make sure pip is up to date
                bat 'python3 -m pip install --upgrade pip'

                // ensure all the zephyr requirements are up to date
                bat 'pip3 install --upgrade --requirement "%ZEPHYR_SCRIPTS%/requirements.txt"'
            }
        }
        stage('Build') {
            steps{
                bat "west build -b pinnacle_100_dvk -d ${env.BUILD_DIR} ${env.APP_SOURCE}"
            }
        }
        stage('Package') {
            steps {
                bat 'md %ARTIFACT_DIR%'

                bat """
                    copy ${env.BUILD_RESULT_HEX_FILE} ${env.ARTIFACT_APP_HEX}
                """
            }
        }
        stage('Archive Artifacts'){
            steps{
                dir(env.ARTIFACT_DIR) {
                    archiveArtifacts env.ARTIFACT_PATTERN
                }
            }
        }
    }
}