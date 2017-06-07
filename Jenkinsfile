pipeline {
    agent any

    stages {
        stage ('git'){
            steps {
                checkout scm
            }
        }

        stage ('pre-analysis') {
            steps {
                sh 'cppcheck --xml-version=2 -j3 --enable=all --std=c++11 `git ls-files "*.hpp" "*.cpp"` 2> cppcheck_report.xml'
                sh 'sloccount --duplicates --wide --details src/*.cpp > sloccount.sc'
                sh 'cccc src/*.cpp || true'
            }
        }

        stage ('sonar-master'){
            when {
                branch 'master'
            }
            steps {
                sh "/opt/sonar-runner/bin/sonar-runner"
            }
        }

        stage ('sonar-branch'){
            when {
                not {
                    branch 'master'
                }
            }
            steps {
                sh "/opt/sonar-runner/bin/sonar-runner -Dsonar.branch=${env.BRANCH_NAME}"
            }
        }
    }

    post {
        always {
            step([$class: 'Mailer',
                notifyEveryUnstableBuild: true,
                recipients: "baptiste.wicht@gmail.com",
                sendToIndividuals: true])
        }
    }
}
