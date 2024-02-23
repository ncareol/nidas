pipeline {

  agent none

  options {
    buildDiscarder(
      logRotator(
        artifactDaysToKeepStr: '7',
        daysToKeepStr: '14',
        numToKeepStr: '2',
        artifactNumToKeepStr: '2'
      )
    )
  }

  triggers {
    pollSCM('H/30 * * * *')
  }

  stages {
    stage('Build NIDAS on all targets') {

      parallel {

        stage('CentOS8_x86_64') {
          agent {
            node {
              label 'CentOS8_x86_64'
            }
          }
          stages {
            stage('Compile and test') {
              steps {
                sh './jenkins.sh test'
              }
            }

            stage('Build RPM packages') {
              steps {
                sh './jenkins.sh build_rpms'
              }
            }

            stage('Sign RPM packages') {
              steps {
                sh './jenkins.sh sign_rpms'
              }
            }

            stage('Push RPM packages to EOL repository') {
              steps {
                sh './jenkins.sh push_rpms'
              }
            }

            stage('Update packages on local host') {
              steps {
                sh './jenkins.sh update_rpms'
              }
            }
          }
        }

        stage('CentOS9_x86_64') {
          agent {
            node {
              label 'CentOS9_x86_64'
            }
          }
          stages {
            stage('Compile and test') {
              steps {
                sh './jenkins.sh test'
              }
            }

            stage('Build RPM packages') {
              steps {
                sh './jenkins.sh build_rpms'
              }
            }

            stage('Sign RPM packages') {
              steps {
                sh './jenkins.sh sign_rpms'
              }
            }

            stage('Push RPM packages to EOL repository') {
              steps {
                sh './jenkins.sh push_rpms'
              }
            }

            stage('Update packages on local host') {
              steps {
                sh './jenkins.sh update_rpms'
              }
            }
          }
        }

        stage('CentOS9_x86_64') {
          agent {
            node {
              label 'CentOS9_x86_64'
            }
          }
          stages {
            stage('Compile and test') {
              steps {
                sh './jenkins.sh test'
              }
            }

            stage('Build RPM packages') {
              steps {
                sh './jenkins.sh build_rpms'
              }
            }

            stage('Sign RPM packages') {
              steps {
                sh './jenkins.sh sign_rpms'
              }
            }

            stage('Push RPM packages to EOL repository') {
              steps {
                sh './jenkins.sh push_rpms'
              }
            }

            stage('Update packages on local host') {
              steps {
                sh './jenkins.sh update_rpms'
              }
            }
          }
        }

        stage('Ubuntu32 (Vortex)') {
          agent {
            node {
              label 'ub32'
            }
          }

          stages {

            stage('Compile and test') {
              steps {
                sh './jenkins.sh test'
              }
            }

            stage('Build Debian packages') {
              steps {
                sh 'scripts/build_dpkg.sh -I bionic i386'
              }
            }
          }
        }
      }
    }
  }

  post {
    changed
    {
      emailext
        from: "granger@ucar.edu",
        to: "granger@ucar.edu, cjw@ucar.edu",
        recipientProviders: [developers(), requestor()],
        subject: "Jenkins build ${env.JOB_NAME}: ${currentBuild.currentResult}",
        body: "Job ${env.JOB_NAME}: ${currentBuild.currentResult}\n${env.BUILD_URL}"
    }
  }

}
