# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json

import mock

from dashboard.api import api_auth
from dashboard.common import datastore_hooks
from dashboard.common import namespaced_stored_object
from dashboard.common import stored_object
from dashboard.common import testing_common
from dashboard.common import utils
from dashboard.pinpoint import test
from dashboard.pinpoint.handlers import new
from dashboard.pinpoint.models import change as change_module
from dashboard.pinpoint.models import job as job_module
from dashboard.pinpoint.models import quest as quest_module


_BASE_REQUEST = {
    'target': 'telemetry_perf_tests',
    'configuration': 'chromium-rel-mac11-pro',
    'benchmark': 'speedometer',
    'bug_id': '12345',
    'start_git_hash': '1',
    'end_git_hash': '3',
}


# TODO: Make this agnostic to the parameters the Quests take.
_CONFIGURATION_ARGUMENTS = {
    'browser': 'release',
    'builder': 'Mac Builder',
    'dimensions': '{"key": "value"}',
    'repository': 'chromium',
    'swarming_server': 'https://chromium-swarm.appspot.com',
}


class _NewTest(test.TestCase):

  def setUp(self):
    super(_NewTest, self).setUp()

    self.SetCurrentUserOAuth(testing_common.INTERNAL_USER)
    self.SetCurrentClientIdOAuth(api_auth.OAUTH_CLIENT_ID_WHITELIST[0])

    key = namespaced_stored_object.NamespaceKey(
        'bot_configurations', datastore_hooks.INTERNAL)
    stored_object.Set(key, {
        'chromium-rel-mac11-pro': _CONFIGURATION_ARGUMENTS
    })


class NewAuthTest(_NewTest):

  @mock.patch.object(api_auth, 'Authorize',
                     mock.MagicMock(side_effect=api_auth.NotLoggedInError()))
  def testPost_NotLoggedIn(self):
    self.SetCurrentUserOAuth(None)

    response = self.Post('/api/new', _BASE_REQUEST, status=401)
    result = json.loads(response.body)
    self.assertEqual(result, {'error': 'User not authenticated'})

  @mock.patch.object(api_auth, 'Authorize',
                     mock.MagicMock(side_effect=api_auth.OAuthError()))
  def testFailsOauth(self):
    self.SetCurrentUserOAuth(testing_common.EXTERNAL_USER)

    response = self.Post('/api/new', _BASE_REQUEST, status=403)
    result = json.loads(response.body)
    self.assertEqual(result, {'error': 'User authentication error'})


@mock.patch('dashboard.services.issue_tracker_service.IssueTrackerService',
            mock.MagicMock())
@mock.patch.object(utils, 'ServiceAccountHttp', mock.MagicMock())
@mock.patch.object(api_auth, 'Authorize', mock.MagicMock())
class NewTest(_NewTest):

  def testPost(self):
    response = self.Post('/api/new', _BASE_REQUEST, status=200)
    result = json.loads(response.body)
    self.assertIn('jobId', result)
    self.assertEqual(
        result['jobUrl'],
        'https://testbed.example.com/job/%s' % result['jobId'])

  def testNoConfiguration(self):
    request = dict(_BASE_REQUEST)
    request.update(_CONFIGURATION_ARGUMENTS)
    del request['configuration']
    response = self.Post('/api/new', request, status=200)
    result = json.loads(response.body)
    self.assertIn('jobId', result)
    self.assertEqual(
        result['jobUrl'],
        'https://testbed.example.com/job/%s' % result['jobId'])

  def testComparisonModeFunctional(self):
    request = dict(_BASE_REQUEST)
    request['comparison_mode'] = 'functional'
    response = self.Post('/api/new', request, status=200)
    result = json.loads(response.body)
    self.assertIn('jobId', result)
    job = job_module.JobFromId(result['jobId'])
    self.assertEqual(job.state.comparison_mode, 'functional')

  def testComparisonModePerformance(self):
    request = dict(_BASE_REQUEST)
    request['comparison_mode'] = 'performance'
    response = self.Post('/api/new', request, status=200)
    result = json.loads(response.body)
    self.assertIn('jobId', result)
    job = job_module.JobFromId(result['jobId'])
    self.assertEqual(job.state.comparison_mode, 'performance')

  def testComparisonModeUnknown(self):
    request = dict(_BASE_REQUEST)
    request['comparison_mode'] = 'invalid comparison mode'
    response = self.Post('/api/new', request, status=400)
    self.assertIn('error', json.loads(response.body))

  def testComparisonMagnitude(self):
    request = dict(_BASE_REQUEST)
    request['comparison_magnitude'] = '123.456'
    response = self.Post('/api/new', request, status=200)
    result = json.loads(response.body)
    self.assertIn('jobId', result)
    job = job_module.JobFromId(result['jobId'])
    self.assertEqual(job.state._comparison_magnitude, 123.456)

  def testQuests(self):
    request = dict(_BASE_REQUEST)
    request['quests'] = ['FindIsolate', 'RunTelemetryTest']
    response = self.Post('/api/new', request, status=200)
    result = json.loads(response.body)
    job = job_module.JobFromId(result['jobId'])
    self.assertEqual(len(job.state._quests), 2)
    self.assertIsInstance(job.state._quests[0], quest_module.FindIsolate)
    self.assertIsInstance(job.state._quests[1], quest_module.RunTelemetryTest)

  def testQuestsString(self):
    request = dict(_BASE_REQUEST)
    request['quests'] = 'FindIsolate,RunTelemetryTest'
    response = self.Post('/api/new', request, status=200)
    result = json.loads(response.body)
    job = job_module.JobFromId(result['jobId'])
    self.assertEqual(len(job.state._quests), 2)
    self.assertIsInstance(job.state._quests[0], quest_module.FindIsolate)
    self.assertIsInstance(job.state._quests[1], quest_module.RunTelemetryTest)

  def testUnknownQuest(self):
    request = dict(_BASE_REQUEST)
    request['quests'] = 'FindIsolate,UnknownQuest'
    response = self.Post('/api/new', request, status=400)
    self.assertIn('error', json.loads(response.body))

  def testWithChanges(self):
    request = dict(_BASE_REQUEST)
    del request['start_git_hash']
    del request['end_git_hash']
    request['changes'] = json.dumps([
        {'commits': [{'repository': 'chromium', 'git_hash': '1'}]},
        {'commits': [{'repository': 'chromium', 'git_hash': '3'}]}])

    response = self.Post('/api/new', request, status=200)
    result = json.loads(response.body)
    self.assertIn('jobId', result)
    self.assertEqual(
        result['jobUrl'],
        'https://testbed.example.com/job/%s' % result['jobId'])

  @mock.patch('dashboard.pinpoint.models.change.patch.FromDict')
  def testWithPatch(self, mock_patch):
    mock_patch.return_value = change_module.GerritPatch(
        'https://lalala', '123', None)
    request = dict(_BASE_REQUEST)
    request['patch'] = 'https://lalala/c/foo/bar/+/123'

    response = self.Post('/api/new', request, status=200)
    result = json.loads(response.body)
    self.assertIn('jobId', result)
    self.assertEqual(
        result['jobUrl'],
        'https://testbed.example.com/job/%s' % result['jobId'])
    mock_patch.assert_called_with(request['patch'])
    job = job_module.JobFromId(result['jobId'])
    self.assertEqual('123', job.gerrit_change_id)
    self.assertEqual('https://lalala', job.gerrit_server)

  def testMissingTarget(self):
    request = dict(_BASE_REQUEST)
    del request['target']
    response = self.Post('/api/new', request, status=400)
    self.assertIn('error', json.loads(response.body))

  def testInvalidTestConfig(self):
    request = dict(_BASE_REQUEST)
    del request['configuration']
    response = self.Post('/api/new', request, status=400)
    self.assertIn('error', json.loads(response.body))

  def testInvalidBug(self):
    request = dict(_BASE_REQUEST)
    request['bug_id'] = 'not_an_int'
    response = self.Post('/api/new', request, status=400)
    self.assertEqual({'error': new._ERROR_BUG_ID},
                     json.loads(response.body))

  def testEmptyBug(self):
    request = dict(_BASE_REQUEST)
    request['bug_id'] = ''
    response = self.Post('/api/new', request, status=200)
    result = json.loads(response.body)
    self.assertIn('jobId', result)
    self.assertEqual(
        result['jobUrl'],
        'https://testbed.example.com/job/%s' % result['jobId'])
    job = job_module.JobFromId(result['jobId'])
    self.assertIsNone(job.bug_id)

  @mock.patch('dashboard.pinpoint.models.change.patch.FromDict')
  def testPin(self, mock_patch):
    mock_patch.return_value = 'patch'
    request = dict(_BASE_REQUEST)
    request['pin'] = 'https://lalala/c/foo/bar/+/123'

    response = self.Post('/api/new', request, status=200)
    result = json.loads(response.body)
    self.assertIn('jobId', result)
    self.assertEqual(
        result['jobUrl'],
        'https://testbed.example.com/job/%s' % result['jobId'])
    mock_patch.assert_called_with(request['pin'])

  def testValidTags(self):
    request = dict(_BASE_REQUEST)
    request['tags'] = json.dumps({'key': 'value'})
    response = self.Post('/api/new', request, status=200)
    result = json.loads(response.body)
    self.assertIn('jobId', result)

  def testInvalidTags(self):
    request = dict(_BASE_REQUEST)
    request['tags'] = json.dumps(['abc'])
    response = self.Post('/api/new', request, status=400)
    self.assertIn('error', json.loads(response.body))

  def testInvalidTagType(self):
    request = dict(_BASE_REQUEST)
    request['tags'] = json.dumps({'abc': 123})
    response = self.Post('/api/new', request, status=400)
    self.assertIn('error', json.loads(response.body))

  def testUserFromParams(self):
    request = dict(_BASE_REQUEST)
    request['user'] = 'foo@example.org'
    response = self.Post('/api/new', request, status=200)
    result = json.loads(response.body)
    job = job_module.JobFromId(result['jobId'])
    self.assertEqual(job.user, 'foo@example.org')

  def testUserFromAuth(self):
    response = self.Post('/api/new', _BASE_REQUEST, status=200)
    result = json.loads(response.body)
    job = job_module.JobFromId(result['jobId'])
    self.assertEqual(job.user, testing_common.INTERNAL_USER.email())
