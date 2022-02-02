pipeline {
	agent {
		docker {
			image 'registry.git.facekapow.dev/anillo-os/anillo-os:latest'
			registryUrl 'https://registry.git.facekapow.dev'
			registryCredentialsId 'jenkins-docker-login'
			alwaysPull true
		}
	}

	triggers {
		githubPush()
	}

	options {
		skipDefaultCheckout()
	}

	stages {
		stage('Start Job') {
			steps {
				scmSkip(deleteBuild: true, skipPattern:'.*\\[ci skip\\].*')

				dir('source') {
					checkout scm
				}

				script {
					def buildNumber = env.BUILD_NUMBER as int
					if (buildNumber > 1) milestone(buildNumber - 1)
					milestone(buildNumber)

					while (fileExists('.job-running')) {
						sleep 1
					}
				}

				touch '.job-running'
			}
		}

		stage('Build x86_64') {
			steps {
				dir('source') {
					sh 'mkdir -p ../build/x86_64'
					sh 'cmake -S . -B ../build/x86_64 -G Ninja -DCMAKE_BUILD_TYPE=Release -DANILLO_ARCH=x86_64'
					sh 'cmake --build ../build/x86_64'
				}

				archiveArtifacts artifacts: 'build/x86_64/disk.img', fingerprint: true
			}
		}

		stage('Build aarch64') {
			steps {
				dir('source') {
					sh 'mkdir -p ../build/aarch64'
					sh 'cmake -S . -B ../build/aarch64 -G Ninja -DCMAKE_BUILD_TYPE=Release -DANILLO_ARCH=aarch64'
					sh 'cmake --build ../build/aarch64'
				}

				archiveArtifacts artifacts: 'build/aarch64/disk.img', fingerprint: true
			}
		}

		// TODO: testing
	}

	post {
		cleanup {
			sh 'rm .job-running'
		}
	}
}
