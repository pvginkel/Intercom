// Builds the Intercom firmware for each hardware version, 1 and 2, and uploads each over the air
// through IoTSupport, at https://iot.ginbov.nl, before it builds the next.
//
// Controller config:
//   - Job: Firmware/Intercom
//   - SCM: pvginkel/Intercom, branch main
//   - Script Path: Jenkinsfile

library identifier: 'JenkinsPipelineUtils', changelog: false

pipeline {
    agent {
        kubernetes {
            inheritFrom 'jenkins-agent-large'
            yamlMergeStrategy merge()
            yaml podYaml(images: [[image: 'espressif/idf:v5.5.3', name: 'idf']])
        }
    }

    options {
        // Without abortPrevious: an upload cut off by an abort leaves the devices on two firmware
        // versions.
        disableConcurrentBuilds()
        skipDefaultCheckout()
        timeout(time: 60, unit: 'MINUTES')
        timestamps()
    }

    triggers {
        githubPush()
    }

    stages {
        stage('Checkout') {
            steps {
                dir('Intercom') {
                    checkout scm
                }
                dir('esp-libs') {
                    git url: 'https://github.com/pvginkel/esp-libs.git', branch: 'main',
                        credentialsId: '5f6fbd66-b41c-405f-b107-85ba6fd97f10'
                }
            }
        }

        // Both hardware versions build into the one build/ directory, so each version is uploaded
        // before the next version's build overwrites it.
        stage('Build firmware (v1)') {
            steps {
                script {
                    espFirmware.build(dir: 'Intercom', hardwareVersion: 1)
                }
            }
        }

        stage('Deploy firmware (v1)') {
            steps {
                script {
                    espFirmware.upload(dir: 'Intercom')
                }
            }
        }

        stage('Build firmware (v2)') {
            steps {
                script {
                    espFirmware.build(dir: 'Intercom', hardwareVersion: 2)
                }
            }
        }

        stage('Deploy firmware (v2)') {
            steps {
                script {
                    espFirmware.upload(dir: 'Intercom')
                }
            }
        }
    }

    post {
        aborted {
            script {
                notify.error("${env.JOB_NAME} #${env.BUILD_NUMBER} aborted (timeout or hand)")
            }
        }
    }
}
